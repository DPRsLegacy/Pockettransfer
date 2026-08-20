using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
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

    public void OnGet(string? returnUrl = null)
    {
        ReturnUrl = returnUrl;
    }

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        var user = await accounts.AuthenticateAsync(Username, Password, ct);
        if (user is null)
        {
            Error = "Invalid username or password.";
            return Page();
        }

        await accounts.EnsureBoxesAsync(user.Id, ct);
        await SignInUserAsync(user);
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
