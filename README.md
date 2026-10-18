# stcc 0.6 — C et assembleur Devpac pour Atari ST

`stcc` est un compilateur croisé autonome écrit en C99, avec son propre
préprocesseur, analyseur C, générateur 68000, assembleur, format objet et éditeur
de liens. Il produit directement des `.TOS`, `.PRG` et `.TTP` relocalisables,
ou une ROM autonome avec le mode `--rom`.
Son fonctionnement ne demande ni compilateur croisé, ni binutils Atari, ni libc
Atari. Le runtime est compilé et assemblé par `stcc` lui-même.

**Cette version couvre un sous-ensemble entier de C89, les champs de bits,
les appels système et les entrées d'interruption ; elle ne revendique pas ANSI
C89 complet.** Les flottants logiciels sont laissés aux exercices, avec la
virgule fixe ; le passage/retour de structures par valeur reste exclu.
Voir [l'état détaillé](docs/STATUS.md) avant de porter du code.

Nouveautés 0.6 : [arguments de `main` pour les TTP](docs/ARGUMENTS.md), y compris
le protocole Atari ARGV ; [macros de cible et de version](docs/PORTABILITY.md)
pour partager du code avec d'autres plateformes. Voir aussi les
[tailles attendues par TOS et la disposition vidéo](docs/TOSCALLS.md).

Ajouts 0.5 : [`register`, registres imposés et choix de pile](docs/REGISTERS.md).
Les variables locales et paramètres peuvent occuper un registre conservé
à travers les appels C ; la pile de démarrage devient configurable.

Ajout 0.4 : [liaison ROM et démarrage autonome du ST](docs/ROM.md), avec
séparation code ROM / données RAM et exemple vidéo sans système préexistant.

Ajout 0.3 : [`stmkdisk`](docs/MKDISK.md), binaire Linux autonome pour créer
une disquette FAT12 `.ST` à partir de fichiers et dossiers, pour Hatari/Steem.

Ajouts 0.2 : [champs de bits](docs/BITFIELDS.md),
[`__interrupt__` et `__naked__`](docs/INTERRUPTS.md), quatre nouveaux exemples.
Le code reste relocalisable : on installe son adresse dans un vecteur Atari.

## Compilation sous Debian

```sh
sudo apt install build-essential python3
make
./stcc --version
./stcc examples/05_gemdos.c -o BONJOUR.TOS
make examples
```

Le compilateur est un exécutable Linux ; `BONJOUR.TOS` est l'exécutable Atari.
Copier ce dernier sur le disque accessible au ST et le lancer depuis GEM.
Une extension `.PRG` ne crée pas à elle seule une application graphique GEM :
il faut écrire les appels AES/VDI nécessaires. `.TTP` utilise le même format,
avec une entrée `int main(int argc, char **argv)` ou `int main(void)`.

```sh
./stcc main.c screen.c input.c -o DEMO.PRG
./stcc main.c routines.s -o DEMO.PRG
./stcc -c main.c -o main.o
./stcc -c routines.s -o routines.o
./stcc main.o routines.o -o DEMO.PRG
```

Les `.o` sont au format interne STCO v1, pas des objets DRI/GST/ELF.
L'interopérabilité initiale avec Devpac passe par les sources assembleur
et la convention d'appel documentée, pas par ses fichiers objets binaires.

## Une disquette à partir des exécutables

```sh
./stmkdisk -o COURS.ST --root examples/bin
./stmkdisk -o DEMO.ST --auto BONJOUR.TOS --add README.md LIREMOI.TXT
hatari --tos /chemin/etos192us.img --disk-a DEMO.ST
```

L'image brute `.ST` contient une disquette FAT12 de 720 Kio par défaut.
Elle s'insère aussi dans le lecteur A de Steem Engine. Les fichiers et dossiers
positionnels sont copiés à la racine ; `--root` copie le contenu d'un dossier.
`--auto` place le programme dans `AUTO` avec une extension `.PRG` pour que TOS
le lance au démarrage. `--size 360` et `--size 800` sont également disponibles.

Les noms dans l'image doivent être au format Atari 8.3 ; `--add SOURCE DEST`
permet un renommage explicite. Une sortie existante demande `--force`.
L'outil est construit et installé avec le compilateur, sans `mkfs`, `mtools`,
montage ni privilège root. Voir [MKDISK.md](docs/MKDISK.md).

## Une ROM sans TOS

```sh
make rom
hatari --machine st --cpulevel 0 --cpuclock 8 --memsize 0 \
    --monitor rgb --patch-tos off --fast-boot off --tos examples/bin/15ROM.IMG
```

Cette commande démarre la ROM créée par stcc, sans image Atari/EmuTOS externe.
Le profil fourni utilise 512 Kio de RAM et une ROM de 192 Kio à `$FC0000`.
L'exemple affiche un message par accès direct à la vidéo, puis s'arrête.
Le mode monochrome est également pris en charge (`--monitor mono`).

```sh
./stcc --rom noyau.c pilotes.s -o NOYAU.IMG --map NOYAU.map
```

Le démarrage configure le MMU du ST, les vecteurs, les variables et la pile,
puis appelle `int main(void)` en superviseur avec les IRQ masquées. Les appels
TOS ne sont pas fournis. Voir [ROM.md](docs/ROM.md) pour le contrat, les adresses,
les options de disposition et l'écriture de son propre démarrage.

## Arguments et code commun

```sh
./stcc examples/18_args.c -o ARGUMENT.TTP
```

Le démarrage TOS découpe la ligne de commande en arguments et conserve les
chemins avec antislash. Le format classique fournit `argv[0]` vide, car TOS
ne transmet pas le nom du programme ; ARGV fournit ce nom et les longues lignes.
`argv[argc]` vaut toujours zéro. Voir [ARGUMENTS.md](docs/ARGUMENTS.md) pour
les citations, les limites et l'exemple de lancement parent/enfant avec Pexec.

```c
#if defined(__ATARI_ST__)
/* Backend Atari : pointeurs 32 bits et ordre big-endian. */
#else
/* Backend du PC ou d'une autre cible. */
#endif
```

`__STCC__`, `__STCC_VERSION__`, `__M68000__`, les tailles `__SIZEOF_INT__` etc.
et `__BYTE_ORDER__` sont prédéfinis. Ils décrivent la cible, pas l'hôte Linux.
L'exemple `19_portable.c` se compile aussi avec `cc -std=c89` sur le PC.

## Variables en registre et pile

```c
register int compteur = 0;                 /* placement automatique */
register(d3) unsigned long ticks = 41UL;     /* D3 imposé */
register(a3) unsigned short *curseur = ecran; /* A3 imposé */
__asm__ { addq.l #1,ticks }
```

L'extension `register(registre)` garantit le stockage dans D2–D7 ou A2–A5
pendant la portée de la variable locale ou du paramètre. Les arguments restent
transmis sur la pile et les registres sont préservés à travers les appels C.
Les calculs intermédiaires, le cadre A6 et SP restent gérés par le compilateur.

```sh
./stcc --stack-size 32768 main.c -o DEMO.PRG
./stcc --stack-top '_ma_pile+32768' main.c -o DEMO.PRG
```

La seconde commande suppose une zone globale `ma_pile` réservant 32 Kio.
Pour changer de pile pendant l'exécution, l'exemple `17_stack.c` utilise un
trampoline `__naked__`, appelle une fonction C puis restaure l'ancienne pile.
Voir [REGISTERS.md](docs/REGISTERS.md) pour les types, l'inline, les limites
et la distinction entre pile utilisateur et pile superviseur.

## Premier appel GEMDOS

```c
void tos_print(const char *message)
{
    __asm__ (d0, d1, a0, a1, cc, memory) {
        move.l  message,-(sp)
        move.w  #9,-(sp)
        trap    #1
        addq.l  #6,sp
    }
}

int main(void)
{
    tos_print("Bonjour Atari ST !\r\n");
    return 0;
}
```

`message` est résolu en accès au paramètre dans le cadre `a6`. Il n'est pas
nécessaire d'écrire son déplacement. Le corps assembleur conserve sa syntaxe
Devpac : pas de chaînes C contenant les instructions.

Un résultat calculé en assembleur doit être écrit dans une variable C puis
retourné en C. La seule modification de `d0` ne constitue pas un `return` C ordinaire.
Pour écrire toute une entrée assembleur, employer `__naked__` ; pour un
gestionnaire C à retour `RTE`, employer `__interrupt__`.

Lire [les règles inline](docs/INLINE.md) pour les registres, la pile et les labels.

## Modèle cible

| Élément | Taille / convention |
|---|---|
| CPU | Motorola 68000 original, sans FPU |
| `char` | 8 bits, signé par défaut |
| `short`, `int`, `enum` | 16 bits |
| `long`, pointeur | 32 bits |
| Ordre des octets | big-endian |
| Alignement des scalaires | 1 pour `char`, 2 pour les autres |
| Structures et unions | alignement 2, taille arrondie au multiple de 2 |
| Pile | descendante, alignée sur 2 octets |
| Arguments | de droite à gauche ; cases de 2 ou 4 octets |
| Résultat scalaire | `d0` |
| Registres volatils | `d0`, `d1`, `a0`, `a1`, CCR |
| Registres préservés | `d2-d7`, `a2-a6`, `sp` à la fin de l'appel |

La convention est proche des conventions C historiques à entiers de 16 bits.
Elle ne garantit pas une compatibilité ABI générale avec tous les compilateurs
Atari. En particulier, un GCC Atari utilisant `int` sur 32 bits n'a pas cette ABI.

Voir [ABI et démarrage](docs/ABI.md).

## Options

| Option | Effet |
|---|---|
| `-o fichier` | Choisir le fichier de sortie |
| `-I dossier`, `-Idossier` | Ajouter un répertoire d'inclusions **C** |
| `-D NOM[=valeur]` | Définir une macro C |
| `-E` | Prétraiter le C vers stdout ou `-o` |
| `-S` | Émettre l'assembleur Devpac |
| `-c` | Émettre un objet STCO par source |
| `-v`, `--version` | Afficher la version et quitter |
| `--help` | Afficher l'aide |
| `--map fichier` | Écrire les adresses finales des symboles |
| `--nostartfiles` | Omettre démarrage, pile et support arithmétique ; entrée au premier octet TEXT |
| `--stack-size N` | Réserver N octets de pile ; défaut 16384, taille paire |
| `--stack-top expression` | Utiliser le sommet d'une zone fournie par le programme |
| `--entry symbole` | Vérifier qu'un symbole est au premier octet TEXT |
| `--rom` | Lier une image ROM brute avec démarrage autonome ST |
| `--rom-base N`, `--rom-size N` | Adresse ROM et capacité en octets ; défaut `$FC0000`, 196608 |
| `--ram-base N`, `--ram-limit N` | Arène DATA/BSS, borne haute exclusive ; défaut `$1000..$78000` |
| `--data-dir dossier` | Choisir le dossier contenant `runtime/` et `include/` |

Les options inconnues sont rejetées. Avec `-E`, `-S` ou `-c`, `-o` exige une seule
source. Sans `-o`, la liaison écrit `a.TOS`, ou `a.rom` avec `--rom`.
`--rom` exige la liaison ; les objets peuvent être produits auparavant avec `-c`.
Les options de pile nécessitent le démarrage automatique et s'excluent.
Les modes `-S` et `-c` prennent le
nom de base de chaque source et écrivent dans le dossier courant.

Le compilateur cherche ses fichiers de support dans `STCC_DATA`, puis à côté
de son exécutable si `runtime/start.s` y existe, puis dans le répertoire fixé
à la compilation (`PREFIX/share/stcc`). `--data-dir` a priorité sur ces choix.

## Tests

```sh
make test
make examples
```

Les tests ne demandent aucun outil croisé. L'exécution CPU utilise en option
le moteur indépendant Unicorn, configuré explicitement en M68000 :

```sh
sudo apt install python3-unicorn
make test
```

Sans Unicorn, les tests d'exécution sont signalés **SKIP** ; la compilation,
les diagnostics, les encodages et le format TOS restent testés. Le chargeur
Python de test simule quelques appels TOS : il ne remplace pas une validation
avec un système d'exploitation.

Pour les tests de chargement et d'exécution sous Hatari :

```sh
sudo apt install hatari
python3 tests/hatari.py --tos /chemin/etos192us.img --examples
```

Les nouveaux tests ROM démarrent les images de stcc sans autre système :

```sh
python3 tests/rom_hatari.py --hatari hatari
python3 tests/registers_hatari.py --hatari hatari --tos /chemin/etos192us.img
python3 tests/arguments_hatari.py --hatari hatari --tos /chemin/etos192us.img
```

Pour la suite TOS, obtenir une ROM EmuTOS auprès de son projet officiel, ou utiliser une ROM TOS
que vous détenez. Les ROMs employées sont celles fournies localement ; le dossier optionnel
`tos/` peut être inclus dans le paquet comme décrit ci-dessous.
Les tests TOS sous Hatari utilisent une machine ST, un M68000 à 8 MHz et 1 Mio de RAM,
avec le pilote vidéo SDL `dummy`. Ils vérifient le code passé à `Pterm`, et
échouent si le programme n'atteint pas la terminaison attendue.

Un contrôle optionnel compare aussi les encodages à un assembleur indépendant :

```sh
python3 tests/reference_asm.py --as m68k-linux-gnu-as
```

L'outil croisé de cette commande est exclusivement une référence de test ;
`stcc`, les exemples et les autres suites n'en dépendent pas.

[VALIDATION.md](docs/VALIDATION.md) décrit uniquement les vérifications
réellement effectuées pour cette livraison.

## Installation et paquet Debian

```sh
make PREFIX=/usr/local
sudo make PREFIX=/usr/local install
```

Pour un paquet construit dans un environnement Debian :

```sh
sudo apt install debhelper build-essential python3
sudo apt install python3-unicorn       # recommandé pour exécuter les tests
dpkg-buildpackage -us -uc -b
```

Ou, avec `dpkg-deb` et sans debhelper :

```sh
make deb
```

Cette seconde voie utilise `scripts/build_deb.py`, détermine les dépendances
avec `dpkg-shlibdeps`, teste le compilateur, puis produit le paquet dans `dist/`.
L'architecture du paquet est celle du compilateur **Linux**, pas celle du ST.

## Nettoyage et images TOS

```sh
make clean     # objets, tests/out, validation, logs, caches Python et temporaires
make fclean    # idem, puis stcc, stmkdisk, examples/bin et dist
```

`clean` conserve les deux outils, les exécutables d'exemples et les paquets.
`fclean` les supprime également. Les sources et le dossier d'entrée `tos/`
sont conservés. Le nettoyage vise les emplacements de travail du projet ;
il ne parcourt pas les dossiers temporaires globaux du système.

Placer les images de TOS dans `tos/` avant de construire le paquet :

```sh
make deb
sudo dpkg -i dist/stcc_0.6.0-1_amd64.deb
hatari --tos /usr/share/stcc/tos/etos192us.img --disk-a DEMO.ST
```

Le nom de la ROM de cette commande est un exemple ; employer celui de votre
fichier. Le paquet installe tout le contenu présent dans `tos/`, sous-dossiers
et fichiers cachés compris, dans **`/usr/share/stcc/tos/`**. Les liens source
sont suivis pour embarquer les octets des images. Les fichiers installés sont
lisibles par tous (0644), les dossiers traversables (0755).

Un dossier `tos/` absent n'empêche pas la construction ou l'installation.
Pour une installation directe, `make PREFIX=/usr install` utilise le même
emplacement ; le préfixe par défaut `/usr/local` donne
`/usr/local/share/stcc/tos/`. `DESTDIR` reste pris en compte pour préparer
les paquets. La copie se fait à leur construction ; l'installation du `.deb`
utilise ces données embarquées et n'a pas besoin du dossier source.

## Organisation

- `src/mkdisk.c` : utilitaire hôte FAT12 indépendant, binaire `stmkdisk`.
- `src/pp.c` : préprocesseur, inclusions et macros.
- `src/lex.c` : tokens C, constantes et capture opaque des blocs Devpac.
- `src/type.c` : types cibles, promotions, compatibilité et dimensions.
- `src/c.c` : déclarateurs, analyse sémantique, AST typé, constantes, initialisations.
- `src/gen.c` : parcours de l'AST et émission du code 68000.
- `src/asm.c` : assembleur Devpac intégré et validation des modes d'adressage.
- `src/object.c` : objets STCO, symboles, fixups, liaisons TOS et ROM.
- `runtime/` : démarrages TOS/ROM, pile, multiplication et division 32 bits.
- `include/` : en-têtes cibles `limits.h`, `stddef.h`, `stdarg.h`, `stcc.h`.
- `examples/` : dix-huit programmes TOS ; binaires dans `examples/bin/`.
- `examples/15_rom/` : démarrage et affichage sans TOS (`make rom`).
- `tests/` : tests positifs, négatifs, encodages et exécution indépendante.

L'AST typé constitue la représentation intermédiaire de cette version. Il n'y a
pas encore de passe IR linéaire. L'émission des expressions repose sur `d0` et
des temporaires empilés ; les variables `register` ont une allocation simple
par portée, les autres variables restent en mémoire.

Aucun code source de compilateur ou d'assembleur existant n'a été intégré.
Les outils de référence et les émulateurs servent exclusivement aux tests.
Les références techniques consultées figurent dans [REFERENCES.md](docs/REFERENCES.md).

Licence MIT : voir `LICENSE`.
