# Administration
The following URLs perform administrative functions on the device. There is no security implemented at all.

* `http://monitor/r` resets the ring buffer to its initialized state. All recorded data is lost.
* `http://monitor/write-eeprom` writes the current programming information (as reported by `/p`) to EEPROM. The saved programs will be loaded into memory automatically whenever the device reboots. This URL is case sensitive (on purpose) and must be spelling/cased exactly as shown.
* `http://monitor/ZAP-eeprom` clears the current programming and the saved mac address from EEPROM. The mac address built into the code will be rewritten into EEPROM (and used as the mac address) at the next boot. This URL is case sensitive (on purpose) and must be spelling/cased exactly as shown.

## About mac addresses
You should modify the code and enter in the mac address that came with your Ethernet shield. If for some reason you do not have a mac address then you can just make one up (at your own risk); follow the guidelines in the comments in the code.

Every time the code boots up it will look for a "magic string" in the EEPROM indicating that a prior bootup of this code saved a mac address in EEPROM. Obviously the first time you ever run this code it won't find that magic string, and will use the mac address programmed into the source code. It will then write that mac address into the EEPROM. Subsequent boot ups will use the EEPROM mac address and will **not** use the `mac[]` address in the source code. This is on purpose so that once you have the correct address set up you don't have to worry that downloading a new/experimental version of the software might overwrite your correct `mac[]` address with whatever generic address is stored inside the source code itself. 

Once you have the magic string in the EEPROM indicating a stored mac address is present, there is only one way to change the mac address if you need to:

1. Edit the source code and put the correct value into the `mac[]` bytes array.
1. Load this code onto your arduino. Note: this will NOT, by itself, change the mac address as the mac address will still be coming from EEPROM (unless this is your first time ever loading the code, in which case you are now done).
1. **Manually** save your programming information. Use the `/p` URL and copy all of the programming strings it displays (this is a pain, you have to manually select just the programming string from each line and save it away).
1. Browse the `http://monitor/ZAP-eeprom` URL. This will erase the stored mac address in the EEPROM (and also erase your saved programming information, which is why you saved it).
1. Reboot or power cycle the arduino. This is very important. **Do NOT use write-eeprom before you reboot.** The goal here is to make the software reboot while there is no magic string in the EEPROM. When the software starts up and doesn't find the magic string in EEPROM it uses (and saves) the `mac[]` address programmed into the source. If you do a write-eeprom after the ZAP-eeprom without an intervening reboot then you still will end up with the previous (old/saved-in-EEPROM) mac address instead of the one programmed into the software.
1. **Manually** restore your programs using the `/p?n=whatever` query strings you saved earlier.

Do this procedure if, for example, you downloaded the software and ran it before you realized you were supposed to put your Ethernet shield's mac address into the `mac[]` variable in the source. Edit the source and use the above procedure to correct that problem.