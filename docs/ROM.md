# Une ROM autonome avec stcc

`--rom` lie le code à une adresse fixe et écrit une image brute, sans en-tête
PRG ni liste de relocations TOS. Le démarrage fourni prépare un **ST original,
68000, avec au moins 512 Kio dans la première banque de RAM**. Il utilise
uniquement ces 512 Kio, même si la machine en possède davantage.

## Premier démarrage

Depuis les sources :

```sh
make
make rom
hatari --machine st --cpulevel 0 --cpuclock 8 --memsize 0 \
    --monitor rgb --patch-tos off --fast-boot off \
    --tos examples/bin/15ROM.IMG
```

Dans Hatari, `--memsize 0` signifie 512 Kio. Employer `--monitor mono` pour un
écran monochrome. L'exemple détecte ce moniteur par le MFP ; sinon il choisit
le mode couleur 320×200 PAL 50 Hz. Il affiche quatre lignes, puis retourne 42.
Le CPU s'arrête avec `STOP` ; le matériel vidéo continue d'afficher l'écran.

L'image fournie à `--tos` est **celle que stcc vient de créer**. Aucune ROM
Atari ou EmuTOS n'est nécessaire. Désactiver les patches TOS de l'émulateur :
l'en-tête annonce une version 1.00 pour que Hatari accepte le fichier, mais
ne fournit aucune compatibilité avec l'API TOS. Sa signature est `STCC`,
pas `ETOS`. Il ne contient aucun code d'un autre système d'exploitation.

Pour son propre programme :

```sh
./stcc --rom noyau.c pilotes.c routines.s -o NOYAU.IMG --map NOYAU.map
```

La forme du point d'entrée C est `int main(void)`. Les fonctions C ordinaires
conservent l'ABI habituelle de stcc ; l'assembleur inline Devpac reste utilisable.

## Ce que prépare le démarrage

Le module `runtime/rom_start.s` est placé en premier dans TEXT. Ses 48 premiers
octets contiennent l'en-tête de ROM et le vecteur de reset, suivi du code 68000.
Sur le ST, le CPU lit ses premiers vecteurs via la ROM ; l'initialisation
remplace immédiatement la valeur de pile provisoire avant de l'utiliser.

Le démarrage masque les interruptions matérielles, réinitialise les
périphériques par l'instruction `RESET`, configure la première banque du MMU
pour 512 Kio, puis installe la pile. Il ne tente pas d'écrire aux adresses
0 à 7, protégées par le ST. Les vecteurs 2 à 255 pointent d'abord vers un
gestionnaire d'arrêt sûr qui ne suppose aucune taille de cadre d'exception.

Il copie ensuite les octets DATA depuis la ROM vers leurs adresses finales
en RAM et efface BSS, pile comprise. Aucun appel n'utilise cette pile avant
la fin de l'effacement. Les primitives entières 32 bits de `runtime/integer.s`
sont liées comme pour un programme TOS, avec une pile de 16 Kio.

`main` est appelé en **mode superviseur, SR = $2700**, avec les IRQ masquées.
Il lui appartient d'initialiser ses périphériques et d'installer leurs
gestionnaires avant d'autoriser leurs interruptions. `__interrupt__` permet
un gestionnaire C à retour RTE ; `__naked__` donne une entrée entièrement
assembleur. Un TRAP synchrone reste utilisable avec les IRQ masquées, à
condition d'avoir installé son propre gestionnaire.

Un retour de `main` arrive à `__stcc_rom_halt` et conserve son résultat dans
D0 avant `STOP`. Une exception non prise en charge arrive à
`__stcc_rom_fault`, qui masque les IRQ et s'arrête sans tenter un `RTE`.

Il n'y a aucun BIOS, XBIOS, GEMDOS, GEM, chargeur de programmes, pilote clavier
ou pilote de disquette implicite. Les appels système des exemples TOS ne sont
donc pas disponibles. L'exemple 15 accède directement au matériel vidéo et
ne commande pas le lecteur de disquette.

## Disposition par défaut

| Zone | Adresses / contenu |
|---|---|
| ROM | `$FC0000..$FEFFFF`, exactement 196 608 octets (192 Kio) |
| TEXT | en-tête, démarrage, code utilisateur, primitives entières, dans la ROM |
| Image DATA | immédiatement après TEXT dans le fichier ROM |
| DATA en RAM | à partir de `$1000`, avec les pointeurs déjà liés à leurs adresses finales |
| BSS en RAM | après DATA, pile de 16 Kio incluse à la fin |
| Limite de l'arène DATA/BSS | `$78000`, exclusive |
| Écran réservé | `$78000..$7FCFF`, 32 000 octets |
| Octets ROM inutilisés | remplissage `$FF` jusqu'à la taille demandée |

Chaque contribution de section commence à une adresse paire. BSS ne consomme
aucun octet dans la ROM. Une référence à une fonction désigne son adresse ROM,
une référence à une variable désigne son adresse RAM : il ne suffit donc pas
de retirer l'en-tête d'un PRG pour obtenir ce résultat.

Le générateur C actuel place aussi les chaînes et les objets initialisés
`const` en DATA : ils occupent une image en ROM **et** une copie en RAM. Il
n'existe pas encore de section C distincte pour les seules données en lecture.
Du contenu constant peut être placé explicitement en TEXT dans un module
assembleur, puis référencé depuis C par une déclaration `extern` adaptée.

## Options de disposition et démarrage personnel

| Option | Valeur par défaut |
|---|---|
| `--rom-base N` | `0xfc0000` |
| `--rom-size N` | `196608` octets |
| `--ram-base N` | `0x1000` |
| `--ram-limit N` | `0x78000`, borne exclusive |

Les nombres sont décimaux ou hexadécimaux préfixés par `0x`. Adresses et tailles
doivent être paires. Le linker contrôle l'espace d'adressage 24 bits, les
chevauchements ROM/RAM, les capacités, les références indéfinies et les portées
de déplacement. La plage RAM doit rester dans `$400..$400000`, hors vecteurs.

Le démarrage ST fourni impose la ROM de 192 Kio à `$FC0000` et une arène RAM
ne dépassant pas `$78000`, pour respecter ses hypothèses matérielles et l'écran.
On peut déplacer le début DATA/BSS dans cette arène. Une autre configuration
demande son propre module d'entrée :

```sh
./stcc --rom --nostartfiles entree.s noyau.c runtime/integer.s \
    --rom-base 0xfc0000 --rom-size 196608 \
    --ram-base 0x2000 --ram-limit 0x78000 \
    --entry debut_rom -o NOYAU.IMG --map NOYAU.map
```

Avec `--nostartfiles`, aucun en-tête ni code de démarrage n'est ajouté, et le
support entier/pile est omis également. L'exemple de commande le réintroduit
explicitement par `runtime/integer.s`. `entree.s` doit fournir son en-tête et
ses vecteurs de reset, initialiser le matériel, copier DATA et effacer BSS.
`--entry` vérifie seulement la position du symbole au premier octet TEXT ;
il ne construit pas un vecteur de reset à sa place.

Les unités peuvent être compilées séparément avec `-c`, puis liées par
`--rom`. Le format STCO v1 est conservé. `--rom` ne se combine pas avec
`-E`, `-S` ou `-c` dans la même invocation. Sans `-o`, la sortie est `a.rom`.
L'assembleur conserve ses directives et ses règles d'expression ; cette
extension ne fournit pas `ORG` ni un langage de script de liaison.

## Symboles du linker

Ces noms sont réservés à la liaison ROM. Les adresses sont les adresses
absolues finales ; les tailles sont des constantes numériques.

| Symbole assembleur | Valeur |
|---|---|
| `__stcc_rom_base` | début de la ROM |
| `__stcc_rom_end` | fin exclusive du fichier ROM complété |
| `__stcc_rom_used_end` | fin exclusive du TEXT et de l'image DATA |
| `__stcc_text_end` | fin exclusive de TEXT |
| `__stcc_data_load` | source de la copie DATA dans la ROM |
| `__stcc_data_start` | destination DATA en RAM |
| `__stcc_data_end` | fin exclusive de DATA en RAM |
| `__stcc_data_size` | taille de DATA, alignements compris |
| `__stcc_bss_start` | début de BSS en RAM |
| `__stcc_bss_end` | fin exclusive de BSS en RAM |
| `__stcc_bss_size` | taille de BSS, alignements compris |
| `__stcc_ram_limit` | limite exclusive de l'arène RAM |

En assembleur, employer `lea symbole,a0` ou `move.l #symbole,d0` pour récupérer
une adresse ; `move.l symbole,d0` lirait la mémoire qui s'y trouve. Depuis C,
`extern unsigned char _stcc_data_start[];` désigne l'adresse du symbole
assembleur `__stcc_data_start`, puisque stcc préfixe les noms C par `_`.

`--map` conserve sa première ligne de tailles TEXT/DATA/BSS, puis indique
l'emplacement et la capacité de la ROM, l'arène RAM et les adresses absolues
des symboles. Aucun ajout d'adresse de chargement TOS n'est nécessaire.

## Vérification et machine physique

```sh
make test
python3 tests/rom_hatari.py --hatari hatari
```

La première commande vérifie les octets et les diagnostics sans émulateur.
La seconde démarre les ROM créées par stcc sous Hatari ST/68000 : initialisation
DATA/BSS après empoisonnement de RAM, pointeurs, calculs 32 bits, pile, vecteurs,
TRAP/RTE et arrêts sur exceptions, vidéo couleur et monochrome. Elle ne demande
aucune image TOS externe. Les rapports sont dans `tests/out/rom*/` et sont
effacés par `make clean`.

La validation a été faite sous Hatari, pas sur un ST physique ni sous Steem.
L'image est un flux d'octets ordonné par adresses. Pour une gravure, le nombre,
la capacité et le découpage des EPROM dépendent de la carte exacte ; le fichier
unique n'est pas une promesse de compatibilité avec chaque jeu de supports.
Les ST très anciens munis d'un simple chargeur TOS sur disquette demandent
également de vérifier leur configuration ROM. Le démarrage fourni ne détecte
pas automatiquement les banques mémoire ni les extensions matérielles.
