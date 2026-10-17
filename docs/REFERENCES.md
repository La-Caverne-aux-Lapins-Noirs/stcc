# Références consultées

Spécifications utilisées pour cette implémentation originale, consultées le
16 et 17 octobre 2026. Aucun code de compilateur, de binutils ni d'assembleur n'a été
repris. Les moteurs externes servent uniquement à vérifier l'exécution.

- Motorola, *M68000 Family Programmer's Reference Manual*, 1992,
  chapitres adressage, instructions entières, instructions privilégiées et
  tableau A-3 du 68000/68008. Les instructions ajoutées au 68010 ou après
  doivent être distinguées du jeu original.
  https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf
- Motorola, *M68000 8-/16-/32-Bit Microprocessors User’s Manual*, table des
  vecteurs, séquence d’exception et trames du 68000 original :
  https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
- HiSoft, *Devpac 3 for Atari ST/STE/TT/Falcon030*, chapitre 3 : champs des
  instructions, expressions, labels locaux, alignement, directives, macros et
  fichiers exécutables Atari. Copie du manuel HiSoft :
  https://info-coach.fr/atari/software/_development/Devpac_v3_Manual_txt.pdf
- Scott Sanders, *The Atari Compendium*, chapitre GEMDOS : format du fichier,
  chaîne de fixups, basepage, démarrage et terminaison. Copie de l'ouvrage :
  https://bus-error.nokturnal.pl/atari_compendium/html/chap2.htm
  BIOS et XBIOS, Setexc, callbacks VBL, chaînage XBRA ; carte mémoire :
  https://bus-error.nokturnal.pl/atari_compendium/html/chap3.htm
  https://bus-error.nokturnal.pl/atari_compendium/html/chap4.htm
  https://bus-error.nokturnal.pl/atari_compendium/html/appenda.htm
  https://bus-error.nokturnal.pl/atari_compendium/html/appendb.htm
- Hatari, documentation officielle d'utilisation et du debugger :
  Formats de disquettes et images brutes .ST :
  https://www.hatari-emu.org/doc/manual.html
  https://www.hatari-emu.org/doc/debugger.html
- Steem Engine, documentation officielle livrée avec les sources, gestionnaire
  de disquettes et format .ST :
  https://github.com/gbouthenot/Steem-Engine/blob/master/steem/doc/readme.txt
- EmuTOS, manuel et diffusion officiels :
  https://emutos.github.io/manual/
  https://sourceforge.net/projects/emutos/files/emutos/1.4/
- Liaison et démarrage ROM : formats d'en-tête vérifiés dans le chargeur Hatari,
  séquence de reset et configuration du MMU ST confrontées à la documentation
  de démarrage EmuTOS. Ces sources de référence ne sont pas intégrées au runtime :
  https://github.com/hatari/hatari/blob/main/src/tos.c
  https://github.com/emutos/emutos/blob/master/bios/startup.S
  https://github.com/emutos/emutos/blob/master/bios/memory.S
  https://github.com/emutos/emutos/blob/master/doc/memdetect.txt

La conformité C89 complète n'est pas revendiquée. Les règles sémantiques
implémentées et les écarts connus sont décrits dans STATUS.md et ABI.md.
