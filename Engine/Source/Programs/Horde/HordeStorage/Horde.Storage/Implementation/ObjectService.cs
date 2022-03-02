// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Horde.Storage.Implementation.Blob;
using Jupiter.Implementation;
using Jupiter.Utils;
using Serilog;

namespace Horde.Storage.Implementation
{
    public interface IObjectService
    {
        Task<(ObjectRecord, BlobContents?)> Get(NamespaceId ns, BucketId bucket, IoHashKey key, string[] fields);
        Task<(ContentId[], BlobIdentifier[])> Put(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier blobHash, CbObject payload);
        Task<(ContentId[], BlobIdentifier[])> Finalize(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier blobHash);

        IAsyncEnumerable<NamespaceId> GetNamespaces();

        Task<bool> Delete(NamespaceId ns, BucketId bucket, IoHashKey key);
        Task<long> DropNamespace(NamespaceId ns);
        Task<long> DeleteBucket(NamespaceId ns, BucketId bucket);
    }

    public class ObjectService : IObjectService
    {
        private readonly IReferencesStore _referencesStore;
        private readonly IBlobService _blobService;
        private readonly IReferenceResolver _referenceResolver;
        private readonly IReplicationLog _replicationLog;
        private readonly IBlobIndex _blobIndex;
        private readonly ILastAccessTracker<LastAccessRecord> _lastAccessTracker;
        private readonly ILogger _logger = Log.ForContext<ObjectService>();

        public ObjectService(IReferencesStore referencesStore, IBlobService blobService, IReferenceResolver referenceResolver, IReplicationLog replicationLog, IBlobIndex blobIndex, ILastAccessTracker<LastAccessRecord> lastAccessTracker)
        {
            _referencesStore = referencesStore;
            _blobService = blobService;
            _referenceResolver = referenceResolver;
            _replicationLog = replicationLog;
            _blobIndex = blobIndex;
            _lastAccessTracker = lastAccessTracker;
        }

        public async Task<(ObjectRecord, BlobContents?)> Get(NamespaceId ns, BucketId bucket, IoHashKey key, string[]? fields = null)
        {
            // if no field filtering is being used we assume everything is needed
            IReferencesStore.FieldFlags flags = IReferencesStore.FieldFlags.All;
            if (fields != null)
            {
                // empty array means fetch all fields
                if (fields.Length == 0)
                    flags = IReferencesStore.FieldFlags.All;
                else
                {
                    flags = fields.Contains("payload")
                        ? IReferencesStore.FieldFlags.IncludePayload
                        : IReferencesStore.FieldFlags.None;
                }
            }
            ObjectRecord o = await _referencesStore.Get(ns, bucket, key, flags);

            // we do not wait for the last access tracking as it does not matter when it completes
            Task lastAccessTask = _lastAccessTracker.TrackUsed(new LastAccessRecord(ns, bucket, key)).ContinueWith(task =>
            {
                if (task.Exception != null)
                {
                    _logger.Error(task.Exception, "Exception when tracking last access record");
                }
            });;

            BlobContents? blobContents = null;
            if ((flags & IReferencesStore.FieldFlags.IncludePayload) != 0)
            {
                if (o.InlinePayload != null && o.InlinePayload.Length != 0)
                {
                    blobContents = new BlobContents(o.InlinePayload);
                }
                else
                {
                    blobContents = await _blobService.GetObject(ns, o.BlobIdentifier);
                }
            }

            return (o, blobContents);
        }

        public async Task<(ContentId[], BlobIdentifier[])> Put(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier blobHash, CbObject payload)
        {
            bool hasReferences = payload.Any(field => field.IsAttachment());

            // if we have no references we are always finalized, e.g. there are no referenced blobs to upload
            bool isFinalized = !hasReferences;

            Task objectStorePut = _referencesStore.Put(ns, bucket, key, blobHash, payload.GetView().ToArray(), isFinalized);

            Task<BlobIdentifier> blobStorePut = _blobService.PutObject(ns, payload.GetView().ToArray(), blobHash);
            Task addRefToBodyTask = _blobIndex.AddRefToBlobs(ns, bucket, key, new [] {blobHash});
            ContentId[] missingReferences = Array.Empty<ContentId>();
            BlobIdentifier[] missingBlobs = Array.Empty<BlobIdentifier>();

            if (hasReferences)
            {
                using IScope _ = Tracer.Instance.StartActive("ObjectService.ResolveReferences");
                try
                {
                    IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, payload);
                    // TODO: we no longer use this as a async enumerable, which will slow down the resolve reference a little bit
                    BlobIdentifier[] referencesArray = await references.ToArrayAsync();
                    Task addRefsTask = _blobIndex.AddRefToBlobs(ns, bucket, key, referencesArray);
                    Task<BlobIdentifier[]> checkReferencesTask = _blobService.FilterOutKnownBlobs(ns, referencesArray);
                    await Task.WhenAll(addRefsTask, checkReferencesTask);
                    missingBlobs = await checkReferencesTask;
                }
                catch (PartialReferenceResolveException e)
                {
                    missingReferences = e.UnresolvedReferences.ToArray();
                }
                catch (ReferenceIsMissingBlobsException e)
                {
                    missingBlobs = e.MissingBlobs.ToArray();
                }
            }

            await Task.WhenAll(objectStorePut, blobStorePut, addRefToBodyTask);
            
            if (missingReferences.Length == 0 && missingBlobs.Length == 0)
            {
                try
                {
                    await _referencesStore.Finalize(ns, bucket, key, blobHash);
                }
                catch (PartialReferenceResolveException e)
                {
                    missingReferences = e.UnresolvedReferences.ToArray();
                }
                catch (ReferenceIsMissingBlobsException e)
                {
                    missingBlobs = e.MissingBlobs.ToArray();
                }

                await _replicationLog.InsertAddEvent(ns, bucket, key, blobHash);
            }

            return (missingReferences, missingBlobs);
        }

        public async Task<(ContentId[], BlobIdentifier[])> Finalize(NamespaceId ns, BucketId bucket, IoHashKey key, BlobIdentifier blobHash)
        {
            (ObjectRecord o, BlobContents? blob) = await Get(ns, bucket, key);
            if (blob == null)
                throw new InvalidOperationException("No blob when attempting to finalize");
            byte[] blobContents = await blob.Stream.ToByteArray();
            CbObject payload = new CbObject(blobContents);

            if (!o.BlobIdentifier.Equals(blobHash))
                throw new ObjectHashMismatchException(ns, bucket, key, blobHash, o.BlobIdentifier);

            bool hasReferences = payload.Any(field => field.IsAttachment());

            ContentId[] missingReferences = Array.Empty<ContentId>();
            BlobIdentifier[] missingBlobs = Array.Empty<BlobIdentifier>();
            if (hasReferences)
            {
                using IScope _ = Tracer.Instance.StartActive("ObjectService.ResolveReferences");
                try
                {
                    IAsyncEnumerable<BlobIdentifier> references = _referenceResolver.ResolveReferences(ns, payload);
                    missingBlobs = await _blobService.FilterOutKnownBlobs(ns, references);
                }
                catch (PartialReferenceResolveException e)
                {
                    missingReferences = e.UnresolvedReferences.ToArray();
                }
                catch (ReferenceIsMissingBlobsException e)
                {
                    missingBlobs = e.MissingBlobs.ToArray();
                }
            }

            if (missingReferences.Length == 0 && missingBlobs.Length == 0)
            {
                await _referencesStore.Finalize(ns, bucket, key, blobHash);
                await _replicationLog.InsertAddEvent(ns, bucket, key, blobHash);
            }

            return (missingReferences, missingBlobs);
        }

        public IAsyncEnumerable<NamespaceId> GetNamespaces()
        {
            return _referencesStore.GetNamespaces();
        }

        public Task<bool> Delete(NamespaceId ns, BucketId bucket, IoHashKey key)
        {
            return _referencesStore.Delete(ns, bucket, key);
        }

        public Task<long> DropNamespace(NamespaceId ns)
        {
            return _referencesStore.DropNamespace(ns);
        }

        public Task<long> DeleteBucket(NamespaceId ns, BucketId bucket)
        {
            return _referencesStore.DeleteBucket(ns, bucket);
        }
    }
}
