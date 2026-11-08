// ThinkBook 16 G7+ IAH: restore the absent I2C0 / HID touchpad namespace.
// Addresses and device-selection values are taken from this machine's DSDT.
// TPDF is the live firmware field declared by the original SSDT-12.
DefinitionBlock ("", "SSDT", 2, "LOCAL", "TBI2C", 0x00000002)
{
    External (\_SB.PC00, DeviceObj)
    External (\TPDF, FieldUnitObj)
    External (\TPDD, FieldUnitObj)

    Scope (\_SB.PC00)
    {
        Device (ZI20)
        {
            Name (_ADR, 0x00150000)
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }
                Return (Zero)
            }

            Device (TPAD)
            {
                Name (_UID, One)
                Name (_S0W, 0x03)
                Name (_CID, "PNP0C50")
                Method (_STA, 0, NotSerialized)
                {
                    If (CondRefOf (\TPDD))
                    {
                        If (\TPDD == One) { Return (Zero) }
                    }
                    If (CondRefOf (\TPDF))
                    {
                        If ((\TPDF >= One) && (\TPDF <= 0x05))
                        {
                            Return (0x0F)
                        }
                    }
                    Return (Zero)
                }
                Method (_HID, 0, Serialized)
                {
                    If (CondRefOf (\TPDF))
                    {
                        Switch (ToInteger (\TPDF))
                        {
                            Case (One) { Return ("SYNA2BA6") }
                            Case (0x02) { Return ("ELAN06FA") }
                            Case (0x03) { Return ("CIRQ1080") }
                            Case (0x04) { Return ("GXTP5100") }
                            Case (0x05) { Return ("CIRQ1080") }
                        }
                    }
                    Return ("MSFT0001")
                }

                Name (RSTD, ResourceTemplate ()
                {
                    I2cSerialBusV2 (0x002C, ControllerInitiated, 400000,
                        AddressingMode7Bit, "\\_SB.PC00.ZI20",
                        0x00, ResourceConsumer, , Exclusive, )
                    GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
                        "\\_SB.GPI0", 0x00, ResourceConsumer, , ) { 0x00B6 }
                })
                Name (RELA, ResourceTemplate ()
                {
                    I2cSerialBusV2 (0x0015, ControllerInitiated, 400000,
                        AddressingMode7Bit, "\\_SB.PC00.ZI20",
                        0x00, ResourceConsumer, , Exclusive, )
                    GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
                        "\\_SB.GPI0", 0x00, ResourceConsumer, , ) { 0x00B6 }
                })
                Method (_CRS, 0, Serialized)
                {
                    // Original firmware GPIO: logical pin 0xB6, active-low level.
                    If (CondRefOf (\TPDF))
                    {
                        If (\TPDF == 0x02) { Return (RELA) }
                    }
                    Return (RSTD)
                }
                Method (_DSM, 4, Serialized)
                {
                    If (Arg0 == ToUUID ("3CDFF6F7-4267-4555-AD05-B30A3D8938DE"))
                    {
                        If (Arg2 == Zero) { Return (Buffer (One) { 0x03 }) }
                        If (Arg2 == One)
                        {
                            If (CondRefOf (\TPDF))
                            {
                                If (\TPDF == 0x02) { Return (One) }
                            }
                            Return (0x20)
                        }
                    }
                    Return (Buffer (One) { 0x00 })
                }
            }
        }
    }
}
