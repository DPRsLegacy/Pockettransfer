using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.Filters;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin;

[RequestFormLimits(MultipartBodyLengthLimit = DatabaseBackupService.MaxBackupBytes)]
[RequestSizeLimit(DatabaseBackupService.MaxBackupBytes)]
public class DatabaseModel(DatabaseBackupService backups) : PageModel
{
    public string Engine { get; private set; } = "";
    public IReadOnlyList<BackupInfo> Backups { get; private set; } = [];

    [BindProperty]
    public string ImportConfirm { get; set; } = "";

    [TempData]
    public string? Status { get; set; }

    public string? Error { get; set; }

    public void OnGet() => Load();

    public async Task<IActionResult> OnGetDownloadAsync(CancellationToken ct)
    {
        try
        {
            var file = await backups.ExportAsync(ct);
            return File(file.Bytes, "application/gzip", file.Name);
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            Load();
            return Page();
        }
    }

    public async Task<IActionResult> OnGetDownloadBackupAsync(string name, CancellationToken ct)
    {
        try
        {
            var file = await backups.ReadBackupAsync(name, ct);
            return File(file.Bytes, "application/gzip", file.Name);
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            Load();
            return Page();
        }
    }

    public async Task<IActionResult> OnPostCreateAsync(CancellationToken ct)
    {
        return await RunAsync(async () =>
        {
            var info = await backups.CreateBackupAsync(ct);
            Status = $"Saved {info.Name} ({DatabaseBackupService.FormatSize(info.SizeBytes)}) on the server.";
        }, ct);
    }

    public async Task<IActionResult> OnPostDeleteAsync(string name, CancellationToken ct)
    {
        return await RunAsync(() =>
        {
            backups.DeleteBackup(name);
            Status = $"Deleted {name}.";
            return Task.CompletedTask;
        }, ct);
    }

    public async Task<IActionResult> OnPostRestoreAsync(string name, CancellationToken ct)
    {
        return await RunRestoreAsync(
            () => backups.RestoreFromBackupAsync(name, DatabaseBackupService.ConfirmPhrase, ct), ct);
    }

    public async Task<IActionResult> OnPostImportAsync(IFormFile? file, CancellationToken ct)
    {
        if (file is null || file.Length == 0)
        {
            Error = "Choose a backup file.";
            Load();
            return Page();
        }

        await using var stream = file.OpenReadStream();
        return await RunRestoreAsync(() => backups.ImportAsync(stream, ImportConfirm, ct), ct);
    }

    public override void OnPageHandlerExecuting(PageHandlerExecutingContext context)
    {
        var feature = HttpContext.Features.Get<IHttpMaxRequestBodySizeFeature>();
        if (feature is { IsReadOnly: false })
            feature.MaxRequestBodySize = DatabaseBackupService.MaxBackupBytes;
        base.OnPageHandlerExecuting(context);
    }

    private async Task<IActionResult> RunRestoreAsync(Func<Task> action, CancellationToken ct)
    {
        try
        {
            await action();
            if (!await backups.UserExistsAsync(ActorId(), ct))
            {
                await HttpContext.SignOutAsync(CookieAuthenticationDefaults.AuthenticationScheme);
                return RedirectToPage("/Account/Login");
            }

            Status = "Database restored. A copy of the previous data was saved under backups.";
            return RedirectToPage();
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            Load();
            return Page();
        }
    }

    private async Task<IActionResult> RunAsync(Func<Task> action, CancellationToken ct)
    {
        try
        {
            await action();
            return RedirectToPage();
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            Load();
            return Page();
        }
    }

    private void Load()
    {
        Engine = backups.EngineName;
        Backups = backups.ListBackups();
    }

    private int ActorId()
    {
        var raw = User.FindFirstValue(ClaimTypes.NameIdentifier);
        if (raw is null || !int.TryParse(raw, out var id))
            throw new InvalidOperationException("Not signed in.");
        return id;
    }
}
