namespace Pockettransfer.Server.Data;

public sealed class User
{
    public int Id { get; set; }
    public required string Username { get; set; }
    public string? Email { get; set; }
    public required string PasswordHash { get; set; }
    public bool IsAdmin { get; set; }
    public DateTimeOffset CreatedAt { get; set; }
    public ICollection<Device> Devices { get; set; } = new List<Device>();
    public ICollection<BankBox> Boxes { get; set; } = new List<BankBox>();
}

public sealed class Device
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public User User { get; set; } = null!;
    public required string Name { get; set; }
    public required string Platform { get; set; }
    public required string TokenHash { get; set; }
    public DateTimeOffset CreatedAt { get; set; }
    public DateTimeOffset? LastUsedAt { get; set; }
}

public sealed class PairingCode
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public User User { get; set; } = null!;
    public required string Code { get; set; }
    public DateTimeOffset ExpiresAt { get; set; }
}

public sealed class BankBox
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public User User { get; set; } = null!;
    public int Index { get; set; }
    public required string Name { get; set; }
    public ICollection<BankPokemon> Pokemon { get; set; } = new List<BankPokemon>();
}

public sealed class BankPokemon
{
    public int Id { get; set; }
    public int BankBoxId { get; set; }
    public BankBox BankBox { get; set; } = null!;
    public int Slot { get; set; }
    public required byte[] PkmData { get; set; }
    public int Format { get; set; }
    public int EntityContext { get; set; }
    public int Species { get; set; }
    public int Form { get; set; }
    public required string Nickname { get; set; }
    public required string OriginalTrainer { get; set; }
    public required string OriginVersion { get; set; }
    public bool IsShiny { get; set; }
    public int Level { get; set; }
    public bool IsLegal { get; set; }
    public required string LegalityReport { get; set; }
    public required string Sha256 { get; set; }
    public DateTimeOffset DepositedAt { get; set; }
    /* False until the console writes the patched save. Pending deposits stay here so a skipped write cannot lose them. */
    public bool Committed { get; set; } = true;
    /* Session holding an in-progress withdraw (still in the bank) or a pending deposit. */
    public Guid? HeldBySessionId { get; set; }
}
