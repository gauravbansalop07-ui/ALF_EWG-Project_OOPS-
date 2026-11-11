# Autonomous Landslide and Flash-Flood Early Warning Grid

A single-file C++17 implementation of the ALF-EWG concept for the CIC-211 Object-Oriented Programming project, Option 1.

## Overview

The program demonstrates a backend for a mountain-region hazard monitoring network. It processes simulated sensor telemetry, calculates hazard scores, dispatches regional siren alerts, sorts stations by risk, and stores incident records in a binary log.

The implementation demonstrates:

- Namespaces, inline hazard calculations, function overloading, and reference-based updates
- Encapsulation, static network metrics, dynamic memory management, deep copying, and operator overloading
- Abstract classes, virtual inheritance, composition, and runtime polymorphism
- Custom exceptions, binary file streams, rollback handling, STL containers and algorithms, a priority functor, and a stream manipulator

## Requirements

- A C++17 compiler
- GCC 9 or later is recommended for standard library support for `std::filesystem`

## Build and Run

Place `ALF_EWG.cpp` in the project folder. If your source file is currently named `project.cpp`, rename it to `ALF_EWG.cpp` or use its current name in the build command.

In PowerShell, change to the project folder and run:

```powershell
g++ -std=c++17 -Wall -Wextra .\ALF_EWG.cpp -o .\ALF_EWG.exe
.\ALF_EWG.exe
```

The program writes `incidents.bin` to the current working directory. Each successful run appends an incident record to that file. Run the executable from a folder where you have permission to create and update files.

## Project Files

- `ALF_EWG.cpp` - Complete program source, including declarations, implementations, and the demonstration entry point
- `README.md` - Project overview and build instructions
- `ALF_EWG_Technical_Report.pdf` - One-page technical summary
- `.gitignore` - Excludes generated executables and incident logs from version control

## Scope

The probe data and siren output are simulated for demonstration. This project is an academic reference implementation and is not connected to field sensors or intended for operational emergency warning.
