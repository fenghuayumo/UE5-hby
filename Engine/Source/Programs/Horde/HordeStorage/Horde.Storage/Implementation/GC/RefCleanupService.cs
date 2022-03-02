﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Dasync.Collections;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class RefCleanupService : PollingService<RefCleanupService.RefCleanupState>
    {
        private readonly IOptionsMonitor<GCSettings> _settings;
        private readonly ILeaderElection _leaderElection;
        private readonly IReferencesStore _referencesStore;
        private volatile bool _alreadyPolling;
        private readonly ILogger _logger = Log.ForContext<RefCleanupService>();


        public class RefCleanupState
        {
            public RefCleanupState(IRefsStore refs, IRefCleanup refCleanup)
            {
                Refs = refs;
                RefCleanup = refCleanup;
            }

            public IRefsStore Refs { get; }
            public IRefCleanup RefCleanup { get; }
        }

        public RefCleanupService(IOptionsMonitor<GCSettings> settings, IRefsStore store, IRefCleanup refCleanup, ILeaderElection leaderElection, IReferencesStore referencesStore) : 
            base(serviceName: nameof(RefCleanupService), settings.CurrentValue.RefCleanupPollFrequency, new RefCleanupState(store, refCleanup))
        {
            _settings = settings;
            _leaderElection = leaderElection;
            _referencesStore = referencesStore;
        }

        protected override bool ShouldStartPolling()
        {
            return _settings.CurrentValue.CleanOldRefRecords;
        }

        public override async Task<bool> OnPoll(RefCleanupState state, CancellationToken cancellationToken)
        {
            if (_alreadyPolling)
                return false;

            _alreadyPolling = true;
            try
            {
                if (!_leaderElection.IsThisInstanceLeader())
                {
                    _logger.Information("Skipped ref cleanup run as this instance was not the leader");
                    return false;
                }
                await foreach (NamespaceId ns in state.Refs.GetNamespaces().WithCancellation(cancellationToken))
                {
                    using IScope scope = Tracer.Instance.StartActive("gc.refs");
                    scope.Span.ResourceName = ns.ToString();

                    _logger.Information("Attempting to run Refs Cleanup of {Namespace}. ", ns);
                    try
                    {
                        List<OldRecord> oldRecords = await state.RefCleanup.Cleanup(ns, cancellationToken);
                        _logger.Information("Ran Refs Cleanup of {Namespace}. Deleted {CountRefRecords}", ns, oldRecords.Count);
                    }
                    catch (Exception e)
                    {
                        _logger.Error("Error running Refs Cleanup of {Namespace}. {Exception}", ns, e);
                    }
                }

                List<NamespaceId>? namespaces = await _referencesStore.GetNamespaces().ToListAsync(cancellationToken);
                await foreach (NamespaceId ns in namespaces)
                {
                    using IScope scope = Tracer.Instance.StartActive("gc.refs");
                    scope.Span.ResourceName = ns.ToString();

                    _logger.Information("Attempting to run Refs Cleanup of {Namespace}. ", ns);
                    try
                    {
                        List<OldRecord> oldRecords = await state.RefCleanup.Cleanup(ns, cancellationToken);
                        _logger.Information("Ran Refs Cleanup of {Namespace}. Deleted {CountRefRecords}", ns, oldRecords.Count);
                    }
                    catch (Exception e)
                    {
                        _logger.Error("Error running Refs Cleanup of {Namespace}. {Exception}", ns, e);
                    }
                }

                return true;

            }
            finally
            {
                _alreadyPolling = false;
            }
        }
    }
}
