using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Account;

public class RegisterModel(AccountService accounts) : PageModel
{
    [BindProperty]
    public string Username { get; set; } = "";

    [BindProperty]
    public string Password { get; set; } = "";

    [BindProperty]
    public string ConfirmPassword { get; set; } = "";

    public string? Error { get; set; }

    public void OnGet()
    {
    }

    public async Task<IActionResult> OnPostAsync(CancellationToken ct)
    {
        if (Password.Length < 8)
        {
            Error = "Password must be at least 8 characters.";
            return Page();
        }
        if (Password != ConfirmPassword)
        {
            Error = "Passwords do not match.";
            return Page();
        }

        try
        {
            var user = await accounts.RegisterAsync(Username, Password, ct);
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
            return RedirectToPage("/Bank/Index");
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            return Page();
        }
    }
}
