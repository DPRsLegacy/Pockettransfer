using System.Security.Claims;
using System.Text.Encodings.Web;
using Microsoft.AspNetCore.Authentication;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Auth;

public sealed class DeviceTokenOptions : AuthenticationSchemeOptions;

public sealed class DeviceTokenHandler(
    IOptionsMonitor<DeviceTokenOptions> options,
    ILoggerFactory logger,
    UrlEncoder encoder,
    AccountService accounts) : AuthenticationHandler<DeviceTokenOptions>(options, logger, encoder)
{
    public const string SchemeName = "DeviceToken";

    protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
    {
        var token = ReadToken();
        if (string.IsNullOrEmpty(token))
            return AuthenticateResult.NoResult();

        var user = await accounts.FindByDeviceTokenAsync(token, Context.RequestAborted);
        if (user is null)
            return AuthenticateResult.Fail("Invalid device token.");

        var identity = new ClaimsIdentity(AccountService.BuildClaims(user), SchemeName);
        var ticket = new AuthenticationTicket(new ClaimsPrincipal(identity), SchemeName);
        return AuthenticateResult.Success(ticket);
    }

    private string? ReadToken()
    {
        if (Request.Headers.TryGetValue("X-Device-Token", out var header) && !string.IsNullOrWhiteSpace(header))
            return header.ToString();
        var auth = Request.Headers.Authorization.ToString();
        if (auth.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
            return auth["Bearer ".Length..].Trim();
        return null;
    }
}
