using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin;

public class IndexModel(AdminService admin) : PageModel
{
    public AdminStats Stats { get; private set; } = new(0, 0, 0, 0, 0, 0, 0);
    public IReadOnlyList<User> RecentUsers { get; private set; } = [];

    [TempData]
    public string? Status { get; set; }

    public string? Error { get; set; }

    public async Task OnGetAsync(CancellationToken ct)
    {
        Stats = await admin.GetStatsAsync(ct);
        RecentUsers = await admin.RecentUsersAsync(8, ct);
    }

    public async Task<IActionResult> OnPostSweepAsync(CancellationToken ct)
    {
        try
        {
            var n = await admin.SweepExpiredPairingCodesAsync(ct);
            Status = n == 0 ? "No expired pairing codes." : $"Removed {n} expired pairing code(s).";
        }
        catch (InvalidOperationException ex)
        {
            Error = ex.Message;
            Stats = await admin.GetStatsAsync(ct);
            RecentUsers = await admin.RecentUsersAsync(8, ct);
            return Page();
        }

        return RedirectToPage();
    }
}
