// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Net.Mime;
using System.Threading.Tasks;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Horde.Storage.Implementation;
using Jupiter;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [FormatFilter]
    [Produces(MediaTypeNames.Application.Json, MediaTypeNames.Application.Octet, CustomMediaTypeNames.UnrealCompactBinary)]
    [Route("api/v1/content-id")]
    public class ContentIdController : ControllerBase
    {
        private readonly IAuthorizationService _authorizationService;
        private IContentIdStore _contentIdStore;

        public ContentIdController(IAuthorizationService authorizationService, IContentIdStore contentIdStore)
        {
            _authorizationService = authorizationService;
            _contentIdStore = contentIdStore;
        }

        /// <summary>
        /// Returns which blobs a content id maps to
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance). Example: `uc4.ddc`</param>
        /// <param name="contentId">The content id to resolve </param>
        /// <param name="format">Optional specifier to set which output format is used json/raw/cb</param>
        [HttpGet("{ns}/{contentId}.{format?}", Order = 500)]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("Cache.read")]
        public async Task<IActionResult> Resolve(NamespaceId ns, ContentId contentId, string? format = null)
        {
            using (IScope _ = Tracer.Instance.StartActive("authorize"))
            {
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }
            BlobIdentifier[]? blobs = await _contentIdStore.Resolve(ns, contentId);

            if (blobs == null)
            {
                return NotFound(new ProblemDetails { Title = $"Unable to resolve content id {contentId} ({ns})." });
            }

            return Ok(new ResolvedContentIdResponse(blobs));
        }

        /// <summary>
        /// Update a content id mapping
        /// </summary>
        /// <param name="ns">Namespace. Each namespace is completely separated from each other. Use for different types of data that is never expected to be similar (between two different games for instance). Example: `uc4.ddc`</param>
        /// <param name="contentId">The content id to resolve </param>
        /// <param name="blobIdentifier">The blob identifier to map the content id to</param>
        /// <param name="contentWeight">The weight of this mapping, higher means more important</param>
        [HttpPut("{ns}/{contentId}/update/{blobIdentifier}/{contentWeight}", Order = 500)]
        [ProducesDefaultResponseType]
        [ProducesResponseType(type: typeof(ProblemDetails), 400)]
        [Authorize("Cache.write")]
        public async Task<IActionResult> UpdateContentIdMapping(NamespaceId ns, ContentId contentId, BlobIdentifier blobIdentifier, int contentWeight)
        {
            using (IScope _ = Tracer.Instance.StartActive("authorize"))
            {
                AuthorizationResult authorizationResult = await _authorizationService.AuthorizeAsync(User, ns, NamespaceAccessRequirement.Name);

                if (!authorizationResult.Succeeded)
                {
                    return Forbid();
                }
            }
            await _contentIdStore.Put(ns, contentId, blobIdentifier, contentWeight);

            return Ok();
        }
    }

    public class ResolvedContentIdResponse
    {
        public BlobIdentifier[] Blobs { get; set; }

        public ResolvedContentIdResponse(BlobIdentifier[] blobs)
        {
            Blobs = blobs;
        }
    }
}
