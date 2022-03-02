﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Jupiter.Common;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.Extensions.Options;

namespace Jupiter
{
    // verifies that you have access to a namespace by checking if you have a corresponding claim to that namespace
    public class NamespaceAuthorizationHandler : AuthorizationHandler<NamespaceAccessRequirement, NamespaceId>
    {
        private readonly INamespacePolicyResolver _namespacePolicyResolver;

        public NamespaceAuthorizationHandler(INamespacePolicyResolver namespacePolicyResolver)
        {
            _namespacePolicyResolver = namespacePolicyResolver;
        }

        protected override Task HandleRequirementAsync(AuthorizationHandlerContext context, NamespaceAccessRequirement requirement,
            NamespaceId namespaceName)
        {
            if (context.User.HasClaim(claim => claim.Type == "AllNamespaces"))
            {
                context.Succeed(requirement);
                return Task.CompletedTask;
            }

            NamespaceSettings.PerNamespaceSettings settings = _namespacePolicyResolver.GetPoliciesForNs(namespaceName);
            // These are ANDed, e.g. all claims needs to be present
            foreach (string expectedClaim in settings.Claims)
            {
                // if expected claim is * then everyone is allowed to use the namespace
                if (expectedClaim == "*")
                {
                    context.Succeed(requirement);
                    continue;
                }

                if (expectedClaim.Contains('='))
                {
                    int separatorIndex = expectedClaim.IndexOf('=');
                    string claimName = expectedClaim.Substring(0, separatorIndex);
                    string claimValue = expectedClaim.Substring(separatorIndex + 1);
                    if (context.User.HasClaim(claim => claim.Type == claimName && claim.Value == claimValue))
                    {
                        context.Succeed(requirement);
                        continue;
                    }
                }
                if (context.User.HasClaim(claim => claim.Type == expectedClaim))
                {
                    context.Succeed(requirement);
                }
            }

            return Task.CompletedTask;
        }
    }

    public class NamespaceAccessRequirement : IAuthorizationRequirement
    {
        public const string Name = "NamespaceAccess";
    }
}
