// ACPI companion for the currently unassociated Intel 00:1f.3 function.
DefinitionBlock ("", "SSDT", 2, "LOCAL", "TBHDA", 0x00000001)
{
    External (\_SB.PC00, DeviceObj)
    Scope (\_SB.PC00)
    {
        Device (ZHDA)
        {
            Name (_ADR, 0x001F0003)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin")) { Return (0x0F) }
                Return (Zero)
            }
            Method (_DSM, 4, Serialized)
            {
                If (Arg2 == Zero) { Return (Buffer (One) { 0x03 }) }
                Return (Package ()
                {
                    "device-id", Buffer (4) { 0xCA, 0x51, 0x00, 0x00 },
                    "class-code", Buffer (4) { 0x00, 0x03, 0x04, 0x00 },
                    "layout-id", Buffer (4) { 0x0B, 0x00, 0x00, 0x00 },
                    "alc-layout-id", Buffer (4) { 0x0B, 0x00, 0x00, 0x00 },
                    "built-in", Buffer (One) { 0x00 }
                })
            }
        }
    }
}
