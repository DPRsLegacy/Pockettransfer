using System.Security.Claims;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Pockettransfer.Server.Services;

namespace Pockettransfer.Server.Pages.Bank;

public class IndexModel(BankService bank, PkHexService pkhex, AccountService accounts) : PageModel
{
    public IReadOnlyList<BoxView> Boxes { get; private set; } = [];
    public int ActiveBox { get; private set; }
    public string? Query { get; private set; }
    public IReadOnlyList<SlotDto> SearchResults { get; private set; } = [];
    public int TotalPokemon { get; private set; }
    public int FilledBoxes { get; private set; }

    public async Task<IActionResult> OnGetAsync(int box = 0, string? q = null, CancellationToken ct = default)
    {
        var raw = User.FindFirstValue(ClaimTypes.NameIdentifier);
        if (raw is null || !int.TryParse(raw, out var userId))
            return RedirectToPage("/Account/Login");

        await accounts.EnsureBoxesAsync(userId, ct);
        var boxes = await bank.GetBoxesAsync(userId, ct);
        Boxes = boxes.Select(b => new BoxView(
            b.Id,
            b.Index,
            b.Name,
            b.Pokemon.OrderBy(p => p.Slot).Select(p => bank.ToDto(p, pkhex, b.Index)).ToList()
        )).ToList();

        Query = string.IsNullOrWhiteSpace(q) ? null : q.Trim();
        ActiveBox = Math.Clamp(box, 0, Math.Max(0, Boxes.Count - 1));
        TotalPokemon = Boxes.Sum(b => b.Slots.Count);
        FilledBoxes = Boxes.Count(b => b.Slots.Count > 0);

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
