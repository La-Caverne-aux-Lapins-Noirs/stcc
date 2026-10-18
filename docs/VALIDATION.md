# Validation de la livraison 0.6.0

Vérifications du 18 octobre 2026. Le patch s'applique aux sources de **0.5.0**.
Cette livraison ajoute les arguments de démarrage et les macros de cible.

| Contrôle | Résultat observé |
|---|---|
| Construction native C99 et avertissements stricts | réussie |
| Suite historique | 130 contrôles réussis, zéro SKIP avec Unicorn M68000 |
| Arguments et macros | 29 contrôles réussis ; 19 programmes de parsing exécutés à deux adresses, plus les contrôles des macros et de l'échec d'allocation |
| Ligne courte | séparateurs, guillemets simples/doubles, arguments vides, citations doublées, antislashs, fin CR/NUL, 126 caractères et argc maximal court |
| ARGV | marqueur 127 exigé, valeur ignorée, nom de programme, chaîne de 300 caractères, 71 arguments et réservation du tableau après Mshrink |
| Erreur de mémoire | échec Malloc simulé : terminaison 127 avant main, sans troncature |
| Source commune | exemple 19 compilé avec stcc et avec cc en C89, retour 42 sur les deux |
| Autres suites | CLI, modes de fonction, 41 contrôles registres/pile, 27 ROM, 28 FAT12 et 5 construction/installation réussis |
| Exemples | 18 programmes TOS construits ; 15 exécutés dans Unicorn ; ROM construite séparément |
| ASan + UBSan | les 28 contrôles de compilation/diagnostic arguments/macros passent avec le compilateur instrumenté ; fuite désactivée, arrêt sur erreur UBSan ; pas d'exécution CPU dans cette passe |
| Paquet amd64 | construit et extrait ; supports C/assembleur et guides installés ; génération TOS/ROM et disquette vérifiée avec le binaire extrait |
| Hatari 2.4.1 / EmuTOS 1.4 US 192 Kio | 54 programmes historiques et 17 exemples exécutés, clavier bloquant écarté |
| Véritable lancement parent/enfant | 4 essais Pexec : ligne vide, citations/chemins, ARGV long et tableau alloué ; résultat de l'enfant et retour au parent vérifiés |
| ROM autonome | 8 démarrages ST/68000 réussis, dont main(int, char **) avec argc=0 et argv[0]=0, sans TOS externe |
| Régression registres/pile sous Hatari | 6 ROM et 4 PRG réussis, dont changement temporaire de pile et interruption avec appel C |

Les tests Pexec utilisent un parent compilé avec stcc qui transmet lui-même
les blocs de commande et d'environnement à GEMDOS. Le debugger attend la
terminaison du parent après celle de l'enfant, et un message confirme le retour
de Pexec. Il ne modifie pas les arguments de l'enfant en mémoire.

Les tests CPU restent un chargeur et des doublures d'appels système ; leur
contrôle Malloc n'est pas un gestionnaire mémoire TOS complet. La passe Hatari
exécute le vrai GEMDOS d'EmuTOS. Aucune mesure de performances ni validation
sur Atari physique ou TOS propriétaire n'est revendiquée.

```sh
make all test examples rom
python3 tests/arguments_hatari.py --hatari hatari --tos /chemin/etos192us.img
python3 tests/hatari.py --hatari hatari --tos /chemin/etos192us.img --examples
python3 tests/rom_hatari.py --hatari hatari
python3 tests/registers_hatari.py --hatari hatari --tos /chemin/etos192us.img
```

Les rapports et journaux se régénèrent dans `tests/out/` et restent des
artefacts effaçables par `make clean`. Les résultats précédents restent datés
ci-dessous.

---

# Validation de la livraison 0.5.0

Vérifications du 18 octobre 2026. Le patch s'applique aux sources de **0.4.0**.
Cette version ajoute le stockage en registre et le choix de pile ; les
résultats des livraisons précédentes sont conservés plus bas avec leur date.

| Contrôle | Résultat observé |
|---|---|
| Construction native C99 et avertissements stricts | réussie |
| Suite historique du compilateur | 130 contrôles réussis, zéro SKIP, Unicorn M68000 disponible |
| Registres et pile | 41 contrôles réussis ; 21 programmes positifs exécutés chacun à deux adresses de chargement dans Unicorn |
| Registres | types 8/16/32 bits, conversions, opérateurs composés, pointeurs, paramètres, récursion, dix registres à travers un appel, scopes et pression de registres |
| Inline | variable automatique protégée, opérande C en registre modifiable, nom matériel d'un registre imposé modifiable |
| Pile PRG | défaut 16 Kio, réserve 32 Kio, cadre local de 20 Kio, pile fournie et conservation des variables placées après son sommet par Mshrink |
| Autres régressions | CLI et modes de fonction réussis ; 27 contrôles ROM, 28 contrôles FAT12 et 5 contrôles de construction/installation réussis |
| Exemples | 16 programmes TOS construits, dont 13 exécutés dans Unicorn ; exemple ROM construit séparément |
| AddressSanitizer + UndefinedBehaviorSanitizer | les 41 contrôles registres/pile passent avec le compilateur instrumenté ; fuite désactivée, arrêt sur erreur UBSan ; exécution CPU écartée de cette passe |
| Paquet Linux amd64 | construit et extrait ; compilation avec les supports installés, nouvelle pile et guide inclus, génération TOS/ROM et disquette vérifiées |
| Hatari 2.4.1, ST / 68000 à 8 MHz | 6 nouvelles exécutions ROM sans TOS externe, plus 4 PRG sous EmuTOS 1.4 US 192 Kio |
| Nouvelle passe Hatari | variables imposées, changement temporaire de pile, grande pile, pile fournie, sommet ROM fixe et gestionnaire TRAP/RTE avec appel C imbriqué |
| Régressions Hatari ROM | les 7 démarrages historiques passent à nouveau : initialisation, vidéo, interruption et exceptions |

La fonction appelée sur une pile secondaire dispose d'un tableau local de
20 Kio, sur une réserve de 32 Kio, puis le trampoline retrouve SP et retourne
à son appelant. Le contrôle de pile fournie vérifie aussi une variable située
après le sommet choisi ; la fin de mémoire conservée reste indépendante de SP.
Ces essais ne mesurent pas les performances et ne valident pas un ST physique.

```sh
make all test examples rom
python3 tests/registers_hatari.py --hatari hatari --tos /chemin/etos192us.img
python3 tests/rom_hatari.py --hatari hatari
```

Les sorties et rapports sont régénérés dans `tests/out/`. Les outils externes
restent des dépendances facultatives de validation, sans code ni binaire ajouté
au projet. Voir REGISTERS.md pour les limites du contrat de stockage.

---

# Validation de la livraison 0.4.0

Vérifications du 17 octobre 2026 sur le mode ROM et le linker partagé TOS/ROM.
Le fichier livré est un patch de sources applicable à **0.3.1**, avec `patch -p1`.

| Contrôle | Résultat observé |
|---|---|
| Construction native C99 et avertissements stricts | réussie |
| Suite historique du compilateur | 130 contrôles réussis, zéro SKIP, Unicorn M68000 disponible |
| CLI, modes de fonction, disquettes, construction/installation | suites réussies, dont 28 contrôles FAT12 et 5 contrôles de construction/installation |
| Exemples TOS | 14 construits, 11 exécutés dans Unicorn ; tous les 14 fichiers sont identiques octet pour octet aux sorties de stcc 0.3.1 |
| Suite ROM sans émulateur | 27 contrôles réussis, dont une image de référence calculée indépendamment, liaison d'objets STCO, adresses ROM/RAM, tailles et diagnostics |
| AddressSanitizer + UndefinedBehaviorSanitizer | les 27 contrôles ROM réussissent ; détection des fuites désactivée, arrêt demandé sur erreur UBSan |
| Paquet Linux | construit, extrait et utilisé pour générer une ROM avec le démarrage et la documentation installés |
| Démarrages ROM sous Hatari 2.4.1 | 7 réussis sur ST / 68000 à 8 MHz, sans image TOS externe ni patches TOS |
| Initialisation RAM | arène remplie de `$A5` avant démarrage, copie DATA et effacement BSS vérifiés, données modifiables et pile restaurée après main |
| Pointeurs et calculs dans la ROM | pointeur vers variable RAM, pointeur vers fonction ROM, multiplication/division/reste 32 bits vérifiés |
| Configuration mémoire | même profil autonome essayé avec Hatari réglé à 512 Kio puis 1 Mio, en utilisant la première banque de 512 Kio |
| Vidéo autonome | couleur PAL 320×200 et monochrome 640×400, pixels de début et fin du message vérifiés dans la mémoire écran |
| Exceptions autonomes | TRAP #3 avec gestionnaire C et retour RTE ; instructions illégales et erreurs de bus vers le gestionnaire d'arrêt |

Les sept essais sont des processus Hatari neufs : le code d'entrée est celui
de la ROM produite par stcc, pas celui d'Atari ou EmuTOS. Le test ne remplace
aucun appel système : il empoisonne la RAM, puis observe les registres et la
mémoire au retour de main ou à l'entrée du gestionnaire d'erreur. L'intégralité
des octets ROM est relue dans Hatari et comparée au fichier initial.

```sh
make all test examples rom
python3 tests/rom_hatari.py --hatari hatari
```

Ces essais ne valident pas une gravure EPROM ni le démarrage sur une carte ST
physique. Le profil matériel fourni suppose une première banque d'au moins
512 Kio ; il ne détecte pas les extensions mémoire. Les rapports et captures
mémoire de test se régénèrent sous `tests/out/` et sont effacés par `make clean`.
Les résultats historiques ci-dessous restent datés des versions concernées.

---

# Validation de la livraison 0.3.0

Vérifications du 17 octobre 2026. La modification fonctionnelle de cette version
est l'ajout du binaire hôte `stmkdisk` ; le compilateur C conserve son comportement.

| Contrôle ajouté | Résultat observé |
|---|---|
| Construction native C99 avec avertissements stricts | réussie |
| AddressSanitizer + UndefinedBehaviorSanitizer sur stmkdisk | les 28 contrôles réussis, contrôle des fuites désactivé |
| Suite stmkdisk | 28 contrôles : FAT12, octets, sous-dossiers de plusieurs clusters, noms, capacités, erreurs et publication |
| Images 360 / 720 / 800 Kio | toutes lues par mtools 4.0.43, toutes validées sans correction par fsck.fat 4.2 en mode Atari |
| Démarrage depuis A sous Hatari 2.4.1 / EmuTOS 1.4 US | les trois géométries démarrent leur programme AUTO |
| Lecture réelle par GEMDOS | fichier de 5 003 octets, plusieurs clusters, recherche dans un sous-dossier de plusieurs clusters et résolution de `..` |
| Écriture réelle par GEMDOS | création de RESULT.BIN ; contenu relu dans l'image après sortie de Hatari |

Le test disquette n'utilise aucun disque GEMDOS provenant d'un dossier Linux.
La ROM reste séparée. Steem Engine n'a pas été exécuté ici ; l'image vise son
format `.ST` brut documenté. Les rapports et journaux `mkdisk-*` de `validation/`
correspondent à ces essais. Les outils FAT externes sont seulement des références
de test, pas des dépendances du générateur.

Reproduction :

```sh
make all test
python3 tests/mkdisk_hatari.py --tos /chemin/etos192us.img \
    --fsck /chemin/fsck.fat --mtools-dir /chemin/bin
```

La livraison est un patch de sources applicable à la version 0.2.0 avec
`patch -p1`. Le binaire `stmkdisk` se construit ensuite par `make`.

Les options fsck/mtools sont facultatives. La suite de régression du compilateur,
la CLI et les conventions de fonction sont également relancées pour vérifier
l'intégration de `make test`.

---

# Validation de la livraison 0.2.0

Vérifications réalisées le 17 octobre 2026, avec le code livré.
Ce relevé distingue les exécutions CPU, celles du système d'exploitation et
les limitations de la couverture.

## Résultats

| Contrôle | Résultat effectivement observé |
|---|---|
| Compilation C99 native avec avertissements stricts | réussie |
| Suite de régression | **130 contrôles réussis, zéro SKIP** dans cet environnement |
| Programmes positifs de cette suite | **54**, chacun exécuté dans Unicorn M68000 à **deux adresses** de chargement |
| Comparaison arithmétique avec C natif | 36 opérations uint32 déterministes, exécutées sur les deux côtés |
| Encodages comparés à GNU m68k-as 2.42 | **1 353** concordances octet pour octet |
| Hatari 2.4.1, machine ST, M68000 à 8 MHz, 1 Mio | **54 programmes** terminés avec le résultat attendu |
| ROM des tests système | **EmuTOS 1.4**, image 192 Kio, variante US |
| Quatorze exemples pédagogiques | tous compilés ; **11 exécutés dans Unicorn**, 3 réservés au système Atari réel émulé |
| Exemples sous Hatari | **13 exécutés** ; clavier bloquant explicitement écarté de cette passe |
| Création de fichier TOS réelle | exemple 7 sous Hatari ; contenu exact de `STCC.TXT` vérifié |
| Mémoire écran réelle émulée | exemple 8 terminé sous Hatari après écriture/restauration |
| Paquet Debian amd64 | construit, contenu extrait et compilation avec les fichiers installés réussie ; dépendance libc6 ≥ 2.34 |
| Champs de bits | largeurs 1 à 16, valeurs signées/unsigned, octets big-endian, voisins préservés, initialisations et opérateurs composés |
| Entrées `__interrupt__` / `__naked__` | émission vérifiée, diagnostics de type et appels interdits vérifiés |
| Exceptions CPU sous Hatari | TRAP #3/#4 imbriqués, appels C, retour anticipé, quinze registres + SR + SP restaurés |
| Chaînage et VBL | entrée brute chaînée exécutée ; callback C ordinaire exécuté pendant cinq VBL réelles |
| CLI `-I -D -E -S -c` et liaison séparée | vérifiées ; trois combinaisons invalides rejetées |
| AddressSanitizer + UndefinedBehaviorSanitizer | suite des 130 contrôles, CLI et modes d’entrée réussis, détection des fuites désactivée |

Le compilateur alloue des objets AST, types et symboles jusqu'à la fin de son
processus. La désactivation du contrôle des fuites ne transforme pas cette
politique de durée de vie en libération incrémentale de la mémoire.

Les 130 contrôles incluent des rejets de C invalide, les encodages unitaires,
le format et la liaison d'objets ; ils ne représentent pas 130 programmes C
positifs. Les 36 opérations différentielles sont regroupées dans un contrôle.
Les 1 353 tests d'encodage constituent une suite séparée.

La passe CPU empoisonne initialement le BSS, vérifie que le runtime le nettoie,
reconstruit les relocations TOS, et observe Mshrink puis Pterm. Les appels TOS
du banc Python sont des doublures limitées. La passe Hatari charge réellement
les exécutables et appelle EmuTOS ; elle ne s'appuie pas sur ces doublures.

Les trois nouveaux exemples système ne passent pas par les doublures TRAP
de Unicorn : Hatari exécute les exceptions et appels TOS. Un essai dédié de
l’exemple 12 a été relancé après l’ajout du retour anticipé. Les détails
figurent dans `validation/interrupt-report.json` et `interrupts-final.log`.

Le test de préservation inline appelle une fonction C depuis l'assembleur,
modifie `d2`, `d3`, `a2` dans le bloc, puis vérifie que l'appelant retrouve ses
valeurs. Les labels locaux répétés, paramètres/locales inline et TRAP GEMDOS
ont également été exécutés.

## Reproduction

```sh
make
make test
make examples
python3 tests/hatari.py --tos /chemin/etos192us.img --examples
python3 tests/reference_asm.py --as m68k-linux-gnu-as
```

Le compilateur fonctionne sans Unicorn, Hatari et GNU m68k-as. Ce sont des
outils **de test**, fournis par leurs projets respectifs. Sans Unicorn,
`make test` indique les exécutions manquantes ; ce n'est pas une réussite CPU.
Aucun de leurs codes source ni de leurs binaires n'est embarqué dans stcc.

Les rapports JSON de cette livraison sont conservés dans `validation/`.
La suite les régénère normalement dans `tests/out/`.

## Limites de validation

- Pas de test sur un Atari ST physique ni avec une ROM Atari TOS propriétaire.
- Pas de test visuel humain du motif vidéo ; on vérifie l'accès et la terminaison.
- Pas de frappe de clavier automatisée sous Hatari : l'exemple 6 est compilé et
  sa liaison BIOS est exécutée avec une doublure renvoyant une touche dans Unicorn.
- Pas de certification ANSI C ni de matrice exhaustive du 68000. Les flottants
  logiciels restent hors du périmètre et ne sont pas testés.
- Le paquet est construit sur l'hôte Linux fourni, et son contenu installé est
  extrait puis utilisé pour compiler un programme. Un environnement Debian
  propre n'a pas été démarré pour tester les règles debhelper.
