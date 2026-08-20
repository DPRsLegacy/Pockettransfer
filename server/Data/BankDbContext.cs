using Microsoft.EntityFrameworkCore;

namespace Pockettransfer.Server.Data;

public sealed class BankDbContext(DbContextOptions<BankDbContext> options) : DbContext(options)
{
    public DbSet<User> Users => Set<User>();
    public DbSet<Device> Devices => Set<Device>();
    public DbSet<PairingCode> PairingCodes => Set<PairingCode>();
    public DbSet<BankBox> BankBoxes => Set<BankBox>();
    public DbSet<BankPokemon> BankPokemon => Set<BankPokemon>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<User>(e =>
        {
            e.HasIndex(x => x.Username).IsUnique();
            e.Property(x => x.Username).HasMaxLength(32);
            e.Property(x => x.Email).HasMaxLength(256);
        });

        modelBuilder.Entity<Device>(e =>
        {
            e.HasIndex(x => x.TokenHash).IsUnique();
            e.Property(x => x.Name).HasMaxLength(64);
            e.Property(x => x.Platform).HasMaxLength(16);
        });

        modelBuilder.Entity<PairingCode>(e =>
        {
            e.HasIndex(x => x.Code).IsUnique();
            e.Property(x => x.Code).HasMaxLength(16);
        });

        modelBuilder.Entity<BankBox>(e =>
        {
            e.HasIndex(x => new { x.UserId, x.Index }).IsUnique();
            e.Property(x => x.Name).HasMaxLength(32);
        });

        modelBuilder.Entity<BankPokemon>(e =>
        {
            e.HasIndex(x => new { x.BankBoxId, x.Slot }).IsUnique();
            e.Property(x => x.Nickname).HasMaxLength(24);
            e.Property(x => x.OriginalTrainer).HasMaxLength(24);
            e.Property(x => x.OriginVersion).HasMaxLength(16);
            e.Property(x => x.Sha256).HasMaxLength(64);
        });
    }
}
