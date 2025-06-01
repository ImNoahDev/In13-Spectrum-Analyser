# Nixie tube spectrum analyser

I have designed an audio spectrum analyser that breaks an audio signal down into 7 frequency bands and displays each band on an in13 nixie tube. My project has Wifi allowing for smart home control. My project has a volume (changes sensitivity) and a brightness potentiometer. Each PCB takes a single audio signal input and 2 can be used for full stereo.

The case uses a laser cut kerf bend to allow the wrap around sheet to be cut in a single part. 

![image.png](/image.png)![image.png](/image-1.png)![image.png](/image-4.png)

![image.png](/image-2.png)

## Assembly instructions.

First manufacture and assemble the PCB according to the KiCad files. Next 3d print the top and bottom parts of the case. place the PCB onto the standoffs of the case with layer B (with the least components) facing upwards. Place the top of the case on and screw all part onto the standoffs. Laser cut the Wrap around and screw it in to the bottom mounting holes of the case while wrapping around the case to give a wooden front finish.

Code can be flashed through the USB port due to the built in USB-Serial converter.

## BOM

Note this is for 2x units (Left and Right) because most PCB manufacturers require at least 2 assembled and the price is not significantly more for 2 vs 1. This allows one per audio channel. The price of the tubes also wildly swings so I have put a placeholder value in that is roughly correct (depending on where you get them).

| Part name         | Cost    | Use                 | URL       |
| ----------------- | ------- | ------------------- | --------- |
| PCB Manufacture   | $11.20  | PCB                 | jlpcb.com |
| Economic PCBA     | $66.22  | Assemble PCB        | jlpcb.com |
| Components (PCBA) | $61.93  | Components for PCBA | jlpcb.com |
| Shipping          | $15     | PCB shipping        | jlpcb.com |
| In13 tubes        | $200    | Tubes               | Ebay      |
| Total:            | $343.35 |                     |           |

| Comment                     | Designator | Footprint                                  | Quantity |
| --------------------------- | ---------- | ------------------------------------------ | -------- |
| MJE340_                     | Q4         | TO126                                      | 1        |
| 10uF                        | C22        | C_1206_3216Metric                          | 1        |
| 4.7k                        | R38        | R_0603_1608Metric                          | 1        |
| 15k 1W                      | R14        | R_2512_6332Metric                          | 1        |
| 820k 0.25w 200v             | R44        | R_1206_3216Metric                          | 1        |
| 820 1/4w                    | R22        | R_1206_3216Metric                          | 1        |
| 0.1uF                       | C28        | C_0201_0603Metric                          | 1        |
| 10v 22uF                    | C5         | C_1206_3216Metric                          | 1        |
| 10uF                        | C11        | C_1206_3216Metric                          | 1        |
| 15k 1W                      | R34        | R_2512_6332Metric                          | 1        |
| USB_C_Receptacle_USB2.0_14P | J1         | USB_C_Receptacle_HCTL_HC-TYPE-C-16P-01A    | 1        |
| SW_Push                     | SW2        | 4312560387X6                               | 1        |
| MJE340_                     | Q2         | TO126                                      | 1        |
| 4.7k                        | R8         | R_0603_1608Metric                          | 1        |
| 0.05 1w                     | R43        | R_2512_6332Metric                          | 1        |
| 0.1uF                       | C15        | C_0603_1608Metric                          | 1        |
| ESP32-WROOM-32              | U3         | ESP32-WROOM-32                             | 1        |
| 15k 1W                      | R19        | R_2512_6332Metric                          | 1        |
| 2.2k 1/4W                   | R26        | R_1206_3216Metric                          | 1        |
| 0.1uF                       | C27        | C_0201_0603Metric                          | 1        |
| 0.1uF                       | C13        | C_0603_1608Metric                          | 1        |
| 2.2k 1/4W                   | R16        | R_1206_3216Metric                          | 1        |
| 2.2k 1/4W                   | R36        | R_1206_3216Metric                          | 1        |
| 10k                         | RV2        | Potentiometer_Alps_RK09K_Single_Horizontal | 1        |
| MSGEQ7                      | U2         | DIP-8_W7.62mm                              | 1        |
| 0.1uf                       | C25        | C_0201_0603Metric                          | 1        |
| SW_Push                     | SW3        | SW_SPST_SKQG_WithStem                      | 1        |
| 4.7k                        | R33        | R_0603_1608Metric                          | 1        |
| 820 1/4w                    | R37        | R_1206_3216Metric                          | 1        |
| MCP6002-xMC                 | U5         | DFN-8-1EP_3x2mm_P0.5mm_EP1.75x1.45mm       | 1        |
| 2.2k 1/4W                   | R31        | R_1206_3216Metric                          | 1        |
| IRFP250N                    | Q8         | TO-247-3_Horizontal_TabDown                | 1        |
| 56 uH                       | L1         | L_Ferrocore_DLG-0403                       | 1        |
| 10k                         | R46        | R_0603_1608Metric                          | 1        |
| 200K                        | R3         | R_0402_1005Metric                          | 1        |
| 4.7k                        | R18        | R_0603_1608Metric                          | 1        |
| 820 1/4w                    | R27        | R_1206_3216Metric                          | 1        |
| MJE340_                     | Q1         | TO126                                      | 1        |
| 0.1uF 10v                   | C10        | C_0402_1005Metric                          | 1        |
| 4.7k                        | R23        | R_0603_1608Metric                          | 1        |
| MJE340_                     | Q6         | TO126                                      | 1        |
| 4.7uF 10v                   | C1         | C_1206_3216Metric                          | 1        |
| 15k 1W                      | R24        | R_2512_6332Metric                          | 1        |
| 10K                         | R7         | R_0603_1608Metric                          | 1        |
| MJE340_                     | Q3         | TO126                                      | 1        |
| 0.1uF                       | C14        | C_0603_1608Metric                          | 1        |
| 0.1uF                       | C19        | C_0603_1608Metric                          | 1        |
| 0.1uF                       | C26        | C_0201_0603Metric                          | 1        |
| 100uF 16v                   | C21        | C_1206_3216Metric                          | 1        |
| 10V 1uF                     | C7         | C_1206_3216Metric                          | 1        |
| 10V 0.1uF                   | C4         | C_0402_1005Metric                          | 1        |
| MCP6002-xMC                 | U4         | DFN-8-1EP_3x2mm_P0.5mm_EP1.75x1.45mm       | 1        |
| 0.1uF                       | C17        | C_0603_1608Metric                          | 1        |
| 470k 1/4w                   | R40        | R_1206_3216Metric                          | 1        |
| 820 1/4w                    | R42        | R_1206_3216Metric                          | 1        |
| 0.1uF                       | C16        | C_0603_1608Metric                          | 1        |
| 10k 0.25w 200v              | R45        | R_1206_3216Metric                          | 1        |
| 15k 1W                      | R10        | R_2512_6332Metric                          | 1        |
| 470k 1/4w                   | R20        | R_1206_3216Metric                          | 1        |
| 47uF 200V                   | C23        | CP_Radial_D13.0mm_P5.00mm                  | 1        |
| SW_Push                     | SW1        | SW_SPST_SKQG_WithStem                      | 1        |
| RXD                         | TP2        | TestPoint_Pad_D1.0mm                       | 1        |
| 10K                         | R4         | R_0603_1608Metric                          | 1        |
| 5.1K                        | R1         | R_0402_1005Metric                          | 1        |
| 470k 1/4w                   | R25        | R_1206_3216Metric                          | 1        |
| 820 1/4w                    | R17        | R_1206_3216Metric                          | 1        |
| 470k 1/4w                   | R30        | R_1206_3216Metric                          | 1        |
| 10V 0.1uF                   | C6         | C_0402_1005Metric                          | 1        |
| 5.1K                        | R2         | R_0402_1005Metric                          | 1        |
| 470k 1/4w                   | R15        | R_1206_3216Metric                          | 1        |
| 0.1uF 10v                   | C9         | C_0402_1005Metric                          | 1        |
| 4.7k                        | R13        | R_0603_1608Metric                          | 1        |
| 10k                         | R47        | R_0603_1608Metric                          | 1        |
| TXD                         | TP1        | TestPoint_Pad_D1.0mm                       | 1        |
| 820 1/4w                    | R12        | R_1206_3216Metric                          | 1        |
| 4.7k                        | R28        | R_0603_1608Metric                          | 1        |
| 470k 1/4w                   | R9         | R_1206_3216Metric                          | 1        |
| 10k                         | RV3        | Potentiometer_Alps_RK09K_Single_Horizontal | 1        |
| MAX1771xSA                  | U11        | SO-8_3.9x4.9mm_P1.27mm                     | 1        |
| 2.2k 1/4W                   | R21        | R_1206_3216Metric                          | 1        |
| 18K                         | R5         | R_0603_1608Metric                          | 1        |
| 33k                         | R6         | R_0603_1608Metric                          | 1        |
| CH340C                      | U7         | SOIC-16_3.9x9.9mm_P1.27mm                  | 1        |
| MJE340_                     | Q5         | TO126                                      | 1        |
| UF4004                      | D2         | DIOAD1055W87L533D272                       | 1        |
| 0.1uF                       | C18        | C_0603_1608Metric                          | 1        |
| 10V 3.3uF                   | C3         | C_1206_3216Metric                          | 1        |
| 2.2k 1/4W                   | R41        | R_1206_3216Metric                          | 1        |
| 0.1uF                       | C12        | C_0603_1608Metric                          | 1        |
| 470k 1/4w                   | R35        | R_1206_3216Metric                          | 1        |
| MCP6002-xMC                 | U6         | DFN-8-1EP_3x2mm_P0.5mm_EP1.75x1.45mm       | 1        |
| 15k 1W                      | R39        | R_2512_6332Metric                          | 1        |
| AMS1117-3.3                 | U1         | SOT-223-3_TabPin2                          | 1        |
| 0.1uF 10v                   | C2         | C_0402_1005Metric                          | 1        |
| 15k 1W                      | R29        | R_2512_6332Metric                          | 1        |
| Conn_Coaxial                | J3         | RCA-Phono_CUI-Devices_RCJ-01X_Horizontal   | 1        |
| 0.1uF                       | C20        | C_0603_1608Metric                          | 1        |
| 33pF                        | C8         | C_0402_1005Metric                          | 1        |
| 0.1uF                       | C29        | C_0201_0603Metric                          | 1        |
| MJE340_                     | Q7         | TO126                                      | 1        |
| 0.1uF 250v                  | C24        | C_1206_3216Metric                          | 1        |
| 820 1/4w                    | R32        | R_1206_3216Metric                          | 1        |
| MCP6002-xMC                 | U18        | DFN-8-1EP_3x2mm_P0.5mm_EP1.75x1.45mm       | 1        |
| 2.2k 1/4W                   | R11        | R_1206_3216Metric                          | 1        |