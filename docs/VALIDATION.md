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
