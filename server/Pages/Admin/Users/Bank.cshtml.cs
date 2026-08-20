using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Admin.Users;

public class BankModel(AdminService admin, BankService bank, PkHexService pkhex) : PageModel
{
    public int UserId { get; private set; }
    public string Username { get; private set; } = "";
    public IReadOnlyList<BoxView> Boxes { get; private set; } = [];
    public int ActiveBox { get; private set; }
    public string? Query { get; private set; }
    public IReadOnlyList<SlotDto> SearchResults { get; private set; } = [];
    public int TotalPokemon { get; private set; }

    [TempData]
    public string? Status { get; set; }

    public string? Error { get; set; }

    public async Task<IActionResult> OnGetAsync(int id, int box = 0, string? q = null, CancellationToken ct = default)
    {
        var detail = await admin.GetUserAsync(id, ct);
        if (detail is null)
            return NotFound();

        UserId = id;
        Username = detail.User.Username;
        await admin.EnsureBoxesAsync(id, ct);
        var boxes = await bank.GetBoxesAsync(id, ct);
        Boxes = boxes.Select(b => new BoxView(
            b.Id,
            b.Index,
            b.Name,
            b.Pokemon.OrderBy(p => p.Slot).Select(p => bank.ToDto(p, pkhex, b.Index)).ToList()
        )).ToList();

        Query = string.IsNullOrWhiteSpace(q) ? null : q.Trim();
        ActiveBox = Math.Clamp(box, 0, Math.Max(0, Boxes.Count - 1));
        TotalPokemon = Boxes.Sum(b => b.Slots.Count);

        if (Query is not null)
        {
            var needle = Query.ToLowerInvariant();
            SearchResults = Boxes
                .SelectMany(b => b.Slots)
                .Where(s =>
                    s.Nickname.Contains(needle, StringComparison.OrdinalIgnoreCase) ||
                    s.SpeciesName.Contains(needle, StringComparison.OrdinalIgnoreCase) ||
                    s.OriginalTrainer.Contains(needle, StringComparison.OrdinalIgnoreCase) ||
                    s.Species.ToString().Contains(needle))
                .OrderBy(s => s.BoxIndex)
                .ThenBy(s => s.Slot)
                .ToList();
        }

        return Page();
    }

    public sealed record BoxView(int Id, int Index, string Name, IReadOnlyList<SlotDto> Slots);
}
