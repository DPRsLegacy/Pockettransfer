using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.RateLimiting;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Account;

public class LoginModel(AccountService accounts) : PageModel
{
    [BindProperty]
    public string Username { get; set; } = "";

    [BindProperty]
    public string Password { get; set; } = "";

    public string? Error { get; set; }

    public string? ReturnUrl { get; set; }

    public void OnGet(string? returnUrl = null, bool limited = false)
    {
        ReturnUrl = returnUrl;
        if (limited)
            Error = LoginThrottle.TooManyAttempts;
    }

    [EnableRateLimiting("login")]
    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        var result = await accounts.AuthenticateAsync(Username, Password, LoginThrottle.ClientKey(HttpContext), ct);
        if (result.RateLimited)
        {
            Error = LoginThrottle.TooManyAttempts;
            Response.StatusCode = StatusCodes.Status429TooManyRequests;
            return Page();
        }

        if (result.User is null)
        {
            Error = "Invalid username or password.";
            return Page();
        }

        await accounts.EnsureBoxesAsync(result.User.Id, ct);
        await SignInUserAsync(result.User);
        return Redirect(string.IsNullOrEmpty(ReturnUrl) ? "/Bank" : ReturnUrl);
    }

    private async Task SignInUserAsync(User user)
    {
        var identity = new ClaimsIdentity(AccountService.BuildClaims(user), CookieAuthenticationDefaults.AuthenticationScheme);
        var props = new AuthenticationProperties
        {
            IsPersistent = true,
            AllowRefresh = true,
            ExpiresUtc = DateTimeOffset.UtcNow.AddDays(14),
        };
        await HttpContext.SignInAsync(
            CookieAuthenticationDefaults.AuthenticationScheme,
            new ClaimsPrincipal(identity),
            props);
    }
}
