using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin.Users;

public class IndexModel(AdminService admin) : PageModel
{
    public IReadOnlyList<UserRow> Users { get; private set; } = [];
    public string? Query { get; private set; }
    public int PageNumber { get; private set; } = 1;
    public int Total { get; private set; }
    public int TotalPages { get; private set; } = 1;

    public async Task OnGetAsync(string? q = null, int p = 1, CancellationToken ct = default)
    {
        Query = string.IsNullOrWhiteSpace(q) ? null : q.Trim();
        var (users, total) = await admin.ListUsersAsync(Query, p, ct);
        Users = users;
        Total = total;
        TotalPages = Math.Max(1, (int)Math.Ceiling(total / (double)AdminService.PageSize));
        PageNumber = Math.Clamp(p, 1, TotalPages);
    }
}
