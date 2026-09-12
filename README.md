# Wax Axis

A fully automated IR ski/snowboard waxing machine, built as a college engineering project at Oregon State University. Wax Axis drives an IR waxer along a linear axis at a controlled, repeatable speed and pass count, so hot-waxing no longer depends on a human hand holding steady pressure and pace down the length of a ski or snowboard base.

## Background

This project started as a solution to a real bottleneck: as president of Oregon State's Ski and Snowboard Club, I help run free wax nights that typically get through 40–60 pairs of skis and boards in about two hours. Most of that time isn't spent waxing — it's spent scraping — but the waxing step itself is still slow and inconsistent when it's done by hand.
The club had already picked up a Mountainflow IR waxing iron, which works great except that it still has to be dragged across each base by hand at a slow, even pace to get good results. Wax Axis automates that motion.
The machine was originally built out of PVC pipe as a quick, cheap proof of concept, and has since been rebuilt on aluminum 2020 extrusion for rigidity and precision.

## Key Features

- **Automatic frame length detection** — the machine senses the length of the frame/travel automatically rather than requiring a manual endpoint setup
- **Iron speed and pass count control** — set how fast the waxer moves and how many passes it makes per run
- **Automated firmware updates** — OTA updates pushed wirelessly, no need to re-flash by cable
- **Full manual control** — jog and control the axis directly when you don't want an automated run
- **Wireless control via Wi-Fi and a web UI** — control the machine from a phone or laptop browser on the local network, no app required
- **Sensorless homing (StallGuard)** — homes itself using motor stall detection instead of physical limit switches
- **Relay-controlled waxer power** — the IR waxer is switched through a relay rather than staying on a dumb outlet

## Hardware Architecture

- **Frame:** 2020 aluminum extrusion rails with a 2020 gantry carriage (originally prototyped in PVC)
- **Motion:** [PD Stepper](https://www.sparkfun.com/pd-stepper.html) — the core of the whole system. It's an all-in-one, USB-PD-powered NEMA 17 driver/controller with a built-in ESP32-S3, a TMC2209 driver (StallGuard sensorless homing), and an AS5600 magnetic encoder for closed-loop position control — this one board is what handles motion control, homing, and the Wi-Fi/web UI.
- **Drive train:** GT2 10mm timing belt with matching drive and idler pulleys along the extrusion rail
- **Waxer:** IR (infrared) waxer mounted to the carriage as the heat source (purchased separately — a Mountainflow IR iron in this build)
- **I/O expansion:** Qwiic GPIO board over I2C — required for the additional inputs/outputs the project needs beyond the PD Stepper's native pins
- **Power control:** Relay board switches waxer power; power socket, cord, and switch assembly bring mains power into the electronics enclosure
- **Cable management:** Drag chains route wiring along the moving axis to prevent snagging and wear

## Getting Started

1. Assemble the frame from the 2020 extrusion and gantry hardware, and mount the GT2 belt and pulleys along the rail.
2. Wire the electronics enclosure: power entry (socket, cord, switch) → relay board → IR waxer; PD Stepper wired to the motor and belt drive; Qwiic GPIO board connected over the Qwiic/I2C bus.
3. Power on the machine and connect to its Wi-Fi captive portal to join it to your local network.
4. Open the machine's web UI in a browser on that network.
5. Let the machine auto-detect the frame/travel length, or home manually via sensorless homing (StallGuard — no limit switches needed).
6. Set your desired speed and pass count, or switch to full manual control, and run a pass.

## Bill of Materials

Rough parts and pricing for the core electronics/motion system (excludes the IR waxer itself, which is purchased separately — this build uses a Mountainflow IR waxing iron).

| Part | Qty | Link | Rough Price |
|---|---|---|---|
| PD Stepper (ESP32-S3 + TMC2209 + AS5600 closed-loop stepper driver/controller) | 1 | [SparkFun](https://www.sparkfun.com/pd-stepper.html) | $64.95 |
| Qwiic GPIO Board | 1 | [SparkFun](https://www.sparkfun.com/sparkfun-qwiic-gpio.html) | $7 |
| Qwiic Cable (flexible, 500mm) | 1 | [SparkFun](https://www.sparkfun.com/flexible-qwiic-cable-500mm.html) | $3 |
| Relay Board (4-channel, optocoupler isolated) | 1 | [Amazon](https://www.amazon.com/dp/B095YFJ69T) | $7 |
| GT2 Timing Belt, 10mm width, 5m length | 1 | [Amazon](https://www.amazon.com/dp/B078STSGSX) | ~$20 |
| GT2 Drive Pulley, 20T, 5mm bore (10mm belt) | 1+ | [Amazon](https://www.amazon.com/gt2-20t-pulley/s?k=gt2+20t+pulley) | ~$8 |
| GT2 Idler Pulley, 20T toothless, 5mm bore (10mm belt) | 1+ | [Amazon](https://www.amazon.com/gt2-20t-pulley/s?k=gt2+20t+pulley) | ~$8 |
| 2020 Extrusion | 1 | McMaster | $90 |
| Mounting Hardware (fasteners, misc.) | 1 | McMaster | $30 |
| 2020 Gantry Carriage | 1 | [Amazon](https://www.amazon.com/dp/B0DYNV76M3) | $23 |
| Power Socket Plug | 1 | [Amazon](https://www.amazon.com/dp/B0BZR7XDVM) | $9.50 |
| Power Cord | 1 | [Amazon](https://www.amazon.com/dp/B09VRLJD7J) | $6 |
| Power Switch | 1 | [Amazon](https://www.amazon.com/dp/B07RRY5MYZ) | $11 |
| Drag Chains | 1 | McMaster | $110 |

> The core electronics/motion stack (PD Stepper, Qwiic board + cable, relay, belt, and pulleys) comes in around $110–120, in line with the ~$150-in-parts figure for the electronics side of the build, excluding the IR waxer and frame materials.

### Final Design Changes to be made

**Hardware**
- Flip around electronics box plugs, or at least some of them
- Shrink the electronics box
- Update the electronics box to be mounted better
- Create some sort of better lock for the iron to go into — a screw or similar
- Create the final assembly with all of the parts in it

**Software**
- Create a way to manually set endpoints
- Create a way to add in saved speeds with labels for the button, such as "Snowboard," "Ski," "Black Topsheet Ski"

## Status

Active development, working through community and coursework feedback before open-sourcing the full design.
