import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BalloonGame"

SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHARACTERISTIC_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"


def handle_notification(sender, data):
    """Called whenever the XIAO sends a BLE event."""

    event = data.decode("utf-8").strip()

    print(f"EVENT: {event}")

    if event == "GROUND_HIT":
        print("💥 BALLOON HIT THE GROUND!")

        # Put your website/game logic here later
        # game_over()0.

    elif event == "RESET":
        print("🔄 GAME RESET")

        # Put your reset logic here later
        # reset_game()

    elif event == "READY":
        print("🟢 BALLOON GAME READY")


async def main():

    print("Scanning for BalloonGame...")

    device = None

    while device is None:

        devices = await BleakScanner.discover(timeout=5)

        for d in devices:

            if d.name == DEVICE_NAME:
                device = d
                break

        if device is None:
            print("BalloonGame not found. Scanning again...")

    print(f"Found: {device.name}")
    print(f"Address: {device.address}")

    async with BleakClient(device) as client:

        print("Connected!")

        await client.start_notify(
            CHARACTERISTIC_UUID,
            handle_notification
        )

        print("Listening for balloon events...")
        print("Hit the balloon or let it fall.")
        print("Press Ctrl+C to stop.")

        # Keep program running
        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":

    try:
        asyncio.run(main())

    except KeyboardInterrupt:
        print("\nDisconnected.")