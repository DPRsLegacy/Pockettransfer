using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin.Users;

public class DetailsModel(AdminService admin) : PageModel
{
    public UserDetail? Detail { get; private set; }

    [BindProperty]
    public string NewPassword { get; set; } = "";

    [BindProperty]
    public string NewUsername { get; set; } = "";

    [BindProperty]
    public string ConfirmUsername { get; set; } = "";

    [BindProperty]
    public string BoxName { get; set; } = "";

    [TempData]
    public string? Status { get; set; }

    [TempData]
    public string? GeneratedPassword { get; set; }

    public string? Error { get; set; }

    public async Task<IActionResult> OnGetAsync(int id, CancellationToken ct)
    {
        return await LoadAsync(id, ct);
    }

    public async Task<IActionResult> OnPostResetPasswordAsync(int id, CancellationToken ct)
    {
        return await RunAsync(id, () => admin.ResetPasswordAsync(id, NewPassword, ct), "Password updated.", ct);
    }

    public async Task<IActionResult> OnPostGeneratePasswordAsync(int id, CancellationToken ct)
    {
        var password = AdminService.GeneratePassword();
        try
        {
            await admin.ResetPasswordAsync(id, password, ct);
            GeneratedPassword = password;
            Status = "New password generated. Copy it now — it will not be shown again.";
            return RedirectToPage(new { id });
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            return await LoadAsync(id, ct);
        }
    }

    public async Task<IActionResult> OnPostRenameAsync(int id, CancellationToken ct)
    {
        return await RunAsync(id, () => admin.RenameUserAsync(id, NewUsername, ct), "Username updated.", ct);
    }

    public async Task<IActionResult> OnPostSetAdminAsync(int id, bool isAdmin, CancellationToken ct)
    {
        var msg = isAdmin ? "Admin access granted." : "Admin access removed.";
        return await RunAsync(id, () => admin.SetAdminAsync(ActorId(), id, isAdmin, ct), msg, ct);
    }

    public async Task<IActionResult> OnPostRevokeDeviceAsync(int id, int deviceId, CancellationToken ct)
    {
        return await RunAsync(id, () => admin.RevokeDeviceAsync(id, deviceId, ct), "Device revoked.", ct);
    }

    public async Task<IActionResult> OnPostRenameBoxAsync(int id, int boxId, CancellationToken ct)
    {
        return await RunAsync(id, () => admin.RenameBoxAsync(id, boxId, BoxName, ct), "Box renamed.", ct);
    }

    public async Task<IActionResult> OnPostRepairBoxesAsync(int id, CancellationToken ct)
    {
        return await RunAsync(id, () => admin.EnsureBoxesAsync(id, ct), "Boxes checked — missing ones were created.", ct);
    }

    public async Task<IActionResult> OnPostDeleteAsync(int id, CancellationToken ct)
    {
        try
        {
            await admin.DeleteUserAsync(ActorId(), id, ConfirmUsername, ct);
            Status = "User deleted.";
            return RedirectToPage("/Admin/Users/Index");
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            return await LoadAsync(id, ct);
        }
    }

    private async Task<IActionResult> RunAsync(int id, Func<Task> action, string ok, CancellationToken ct)
    {
        try
        {
            await action();
            Status = ok;
            return RedirectToPage(new { id });
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            return await LoadAsync(id, ct);
        }
    }

    private async Task<IActionResult> LoadAsync(int id, CancellationToken ct)
    {
        Detail = await admin.GetUserAsync(id, ct);
        if (Detail is null)
            return NotFound();
        if (string.IsNullOrEmpty(NewUsername))
            NewUsername = Detail.User.Username;
        return Page();
    }

    private int ActorId() => AdminService.CurrentUserId(User);
}
