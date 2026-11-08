/* Native AppleSMCLMU enumeration only. Real illuminance is supplied by
 * ISHLightSensor over SMC ALV0; no synthetic ACPI _ALI/_ALR values.
 * Device layout adapted from OpenCorePkg SSDT-ALS0 (BSD-3-Clause).
 */
DefinitionBlock ("", "SSDT", 2, "LOCAL", "ISHALS", 1)
{
    Scope (\_SB)
    {
        Device (ALS0)
        {
            Name (_HID, "ACPI0008")
            Name (_CID, "smc-als")
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin")) { Return (0x0F) }
                Return (Zero)
            }
        }
    }
}
