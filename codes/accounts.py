import sys
import time
import starkdb
from starkdb import StarkDBNotFound, StarkDBError

DB_NAME = "accounts"
ACCOUNT_TYPE = "account"

FIELDS = [
    {"name": "owner", "type": 2, "size": 64},   # string(64)
    {"name": "balance", "type": 1, "size": 4},  # int (cents)
    {"name": "pin", "type": 1, "size": 4},      # int
    {"name": "created", "type": 1, "size": 4},  # int (unix ts)
]


# ---------- helpers ----------

def money(cents: int) -> str:
    return f"${cents / 100:,.2f}"


def parse_money(text: str) -> int:
    return int(round(float(text.replace("$", "").replace(",", "")) * 100))


def banner(title: str):
    print("\n" + "=" * 50)
    print(title)
    print("=" * 50)


def pause():
    input("\n[Enter] to continue...")


# ---------- core ops ----------

def ensure_type(db):
    """Define the account type if it doesn't exist yet."""
    try:
        db.get_typed(ACCOUNT_TYPE, 0)  # probe; will raise NotFound if undefined
    except StarkDBNotFound:
        pass
    # define_type is idempotent-ish here: re-defining overwrites the definition,
    # which is fine and keeps this script re-runnable.
    db.define_type(ACCOUNT_TYPE, FIELDS)


def account_exists(db, acct_id: int) -> bool:
    try:
        db.get_typed(ACCOUNT_TYPE, acct_id)
        return True
    except StarkDBNotFound:
        return False


def create_account(db, owner: str, pin: int, initial_cents: int = 0) -> int:
    acct_id = next_id(db)
    created = int(time.time())
    record = f"owner={owner} balance={initial_cents} pin={pin} created={created}"
    db.add_typed(ACCOUNT_TYPE, acct_id, record)
    return acct_id


def next_id(db) -> int:
    # use a string key as a counter so IDs never collide with typed-record keys
    raw = db.get_str("meta:next_id", default=b"1")
    nid = int(raw.decode()) if raw else 1
    db.put_str("meta:next_id", str(nid + 1).encode())
    return nid


def get_account(db, acct_id: int) -> dict:
    raw = db.get_typed(ACCOUNT_TYPE, acct_id)  # raises NotFound
    data = {}
    for tok in raw.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            data[k] = v.strip('"')
    return {
        "id": acct_id,
        "owner": data.get("owner", "?"),
        "balance": int(data.get("balance", "0")),
        "pin": int(data.get("pin", "0")),
        "created": int(data.get("created", "0")),
    }


def save_account(db, acct: dict):
    record = (
        f"owner={acct['owner']} balance={acct['balance']} "
        f"pin={acct['pin']} created={acct['created']}"
    )
    db.add_typed(ACCOUNT_TYPE, acct["id"], record)


def list_accounts(db):
    ids = []
    for key in db:
        try:
            acct = get_account(db, key)
            ids.append(acct)
        except StarkDBNotFound:
            continue
    return sorted(ids, key=lambda a: a["id"])


# ---------- UI ----------

def ui_create(db):
    banner("Create Account")
    owner = input("Owner name: ").strip()
    if not owner:
        print("! name required")
        return
    try:
        pin = int(input("PIN (4 digits): ").strip())
    except ValueError:
        print("! PIN must be a number")
        return
    try:
        initial = parse_money(input("Initial deposit (e.g. 100.00): ").strip() or "0")
    except ValueError:
        print("! invalid amount")
        return
    if initial < 0:
        print("! initial deposit cannot be negative")
        return

    acct_id = create_account(db, owner, pin, initial)
    print(f"\n✓ Account #{acct_id} created for {owner} with {money(initial)}")


def ui_list(db):
    banner("All Accounts")
    accounts = list_accounts(db)
    if not accounts:
        print("(no accounts yet)")
        return
    print(f"{'ID':>4}  {'Owner':<20} {'Balance':>12}  Created")
    print("-" * 60)
    for a in accounts:
        ts = time.strftime("%Y-%m-%d %H:%M", time.localtime(a["created"]))
        print(f"{a['id']:>4}  {a['owner']:<20} {money(a['balance']):>12}  {ts}")


def ui_deposit(db):
    banner("Deposit")
    try:
        acct_id = int(input("Account ID: ").strip())
        amount = parse_money(input("Amount: ").strip())
    except ValueError:
        print("! invalid input")
        return
    try:
        acct = get_account(db, acct_id)
    except StarkDBNotFound:
        print(f"! account #{acct_id} not found")
        return
    db.begin()
    try:
        acct["balance"] += amount
        save_account(db, acct)
        db.commit()
    except StarkDBError as e:
        db.rollback()
        print(f"! transaction failed: {e}")
        return
    print(f"\n✓ Deposited {money(amount)} to #{acct_id}. New balance: {money(acct['balance'])}")


def ui_withdraw(db):
    banner("Withdraw")
    try:
        acct_id = int(input("Account ID: ").strip())
        pin = int(input("PIN: ").strip())
        amount = parse_money(input("Amount: ").strip())
    except ValueError:
        print("! invalid input")
        return
    try:
        acct = get_account(db, acct_id)
    except StarkDBNotFound:
        print(f"! account #{acct_id} not found")
        return
    if acct["pin"] != pin:
        print("! wrong PIN")
        return
    if amount > acct["balance"]:
        print(f"! insufficient funds (balance: {money(acct['balance'])})")
        return
    db.begin()
    try:
        acct["balance"] -= amount
        save_account(db, acct)
        db.commit()
    except StarkDBError as e:
        db.rollback()
        print(f"! transaction failed: {e}")
        return
    print(f"\n✓ Withdrew {money(amount)} from #{acct_id}. New balance: {money(acct['balance'])}")


def ui_transfer(db):
    banner("Transfer")
    try:
        src_id = int(input("From account ID: ").strip())
        dst_id = int(input("To account ID: ").strip())
        pin = int(input("From account PIN: ").strip())
        amount = parse_money(input("Amount: ").strip())
    except ValueError:
        print("! invalid input")
        return
    if src_id == dst_id:
        print("! source and destination are the same")
        return
    try:
        src = get_account(db, src_id)
        dst = get_account(db, dst_id)
    except StarkDBNotFound as e:
        print(f"! {e}")
        return
    if src["pin"] != pin:
        print("! wrong PIN")
        return
    if amount > src["balance"]:
        print(f"! insufficient funds (balance: {money(src['balance'])})")
        return

    # NOTE: rollback() does not truly undo in this STARKDB version (CONTEXT §18.1),
    # so we only touch records after all validations pass.
    db.begin()
    try:
        src["balance"] -= amount
        dst["balance"] += amount
        save_account(db, src)
        save_account(db, dst)
        db.commit()
    except StarkDBError as e:
        db.rollback()
        print(f"! transfer failed: {e}")
        return
    print(f"\n✓ Transferred {money(amount)} from #{src_id} to #{dst_id}")


def ui_delete(db):
    banner("Delete Account")
    try:
        acct_id = int(input("Account ID: ").strip())
    except ValueError:
        print("! invalid input")
        return
    try:
        acct = get_account(db, acct_id)
    except StarkDBNotFound:
        print(f"! account #{acct_id} not found")
        return
    if acct["balance"] > 0:
        print(f"! cannot delete #{acct_id}: balance is {money(acct['balance'])}")
        return
    confirm = input(f"Delete account #{acct_id} ({acct['owner']})? [y/N] ").strip().lower()
    if confirm != "y":
        print("cancelled")
        return
    db.delete_str(f"{ACCOUNT_TYPE}:{acct_id}")  # raw record key
    print(f"\n✓ Account #{acct_id} deleted")


def ui_stats(db):
    banner("Database Stats")
    s = db.stats()
    print(f"keys_count   : {s['keys_count']}  (may undercount on multi-leaf trees)")
    print(f"btree_height : {s['btree_height']}  (hard-coded to 1)")
    print(f"data_size    : {s['data_size']:,} bytes")
    print(f"page_count   : {s['page_count']}")
    print(f"closed       : {db.closed}")
    print(f"last error   : {db.error() or '(none)'}")


def ui_batch_demo(db):
    """Show put_batch / get_batch — stores numeric keys 900000+."""
    banner("Batch Demo")
    batch = {900000 + i: f"note-{i}".encode() for i in range(5)}
    db.put_batch(batch)
    fetched = db.get_batch(list(batch.keys()) + [999999])
    print(f"stored {len(batch)} items, fetched {len(fetched)} (missing silently skipped)")
    for k, v in sorted(fetched.items()):
        print(f"  {k} -> {v!r}")
    # clean up so they don't pollute the account listing
    for k in batch:
        db.delete(k)
    print("cleaned up batch keys")


# ---------- menu ----------

MENU = """
┌──────────────────────────────────┐
│   STARK Account Manager          │
├──────────────────────────────────┤
│  1) Create account               │
│  2) List accounts                │
│  3) Deposit                      │
│  4) Withdraw                     │
│  5) Transfer                     │
│  6) Delete account               │
│  7) Stats                        │
│  8) Batch demo                   │
│  9) Quit                         │
└──────────────────────────────────┘
"""


def main():
    print(f"Opening database '{DB_NAME}' (creates {DB_NAME}.idx/.dat in CWD)")
    try:
        db = starkdb.Database(DB_NAME)
    except StarkDBError as e:
        print(f"FATAL: could not open database: {e}")
        sys.exit(1)

    ensure_type(db)
    print(f"Loaded {len(list_accounts(db))} existing account(s).")

    actions = {
        "1": ui_create,
        "2": ui_list,
        "3": ui_deposit,
        "4": ui_withdraw,
        "5": ui_transfer,
        "6": ui_delete,
        "7": ui_stats,
        "8": ui_batch_demo,
    }

    try:
        while True:
            print(MENU)
            choice = input("Choice: ").strip()
            if choice == "9":
                break
            action = actions.get(choice)
            if action is None:
                print("! unknown choice")
                continue
            try:
                action(db)
            except StarkDBError as e:
                print(f"! database error: {e}")
            except KeyboardInterrupt:
                print("\n(cancelled)")
                continue
            if choice != "2":  # list doesn't need a pause
                pause()
    finally:
        db.sync()
        db.close()
        print("\nDatabase synced and closed. Bye.")


if __name__ == "__main__":
    main()