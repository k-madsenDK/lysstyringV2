# lysstyringV2

**Lysstyring til mit hus**

Dette projekt er en avanceret lysstyringsløsning til mit hus, skrevet primært i C++ (med enkelte C-komponenter). Systemet er designet til at automatisere og styre belysningen i hjemmet effektivt og fleksibelt.

## Funktioner

- Automatisk tænd/sluk af lys baseret på tid og sensorer
- Manuel styring via fysiske knapper eller evt. app/web-interface
- Mulighed for scenarier (f.eks. "Film-aften", "Vækning", "Ferie")
- Energibesparende funktioner (sluk automatisk når ingen er hjemme)
- Kan udvides med flere sensorer eller aktuatorer

## Teknologi

- **Sprog:** C++ (primært), C
- **Hardware:** Typisk Arduino, ESP32, eller lignende microcontroller
- **Sensorer:** Bevægelsessensorer, lyssensorer, evt. internetklokke
- **Aktuatorer:** Relæer eller smart-lys

## Installation

1. **Klon repositoriet:**
   ```bash
   git clone https://github.com/k-madsenDK/lysstyringV2.git
   ```
2. **Åbn projektet i din foretrukne IDE** (f.eks. Arduino IDE eller PlatformIO).
3. **Konfigurer hardware og forbind sensorer/aktuatorer** i henhold til koden og egne behov.
4. **Upload koden til din microcontroller.**
5. **Test og tilpas løsningen** til dine ønsker og lokale forhold.

## Brug

- Systemet kan køre helt automatisk, men du kan tilpasse logikken i koden, så det passer til dit hjem.
- Tilføj flere scenarier eller funktioner efter behov.
- Følg koden og kommentarerne for at udvide systemet.

## Mappestruktur

- `/src` – Kildekode (C++/C-filer)
- `/lib` – Eksterne biblioteker
- `/docs` – Dokumentation og evt. diagrammer

## Bidrag

Dette projekt er primært til privat brug, men du er velkommen til at foreslå forbedringer eller rapportere problemer via Issues.

## Kontakt

Udviklet af [k-madsenDK](https://github.com/k-madsenDK).

---

*Dette er et hobbyprojekt til hjemmeautomatisering og leveres som det er, uden garanti.*
