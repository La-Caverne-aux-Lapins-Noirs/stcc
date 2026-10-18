# Tailles des types, vidéo et appels Atari

## Le 68000 et les entiers C

Le 68000 possède des registres de 32 bits et des instructions octet, mot et
long mot. stcc conserve `int` sur 16 bits et `long` sur 32 bits. Les pointeurs
occupent 32 bits ; le ST original adresse physiquement 24 bits. Il n'existe pas
de limite de tableau à 64 Kio imposée par le type pointeur.

Pour parcourir une grande zone, utiliser un pointeur avancé, ou choisir un
indice `unsigned int` jusqu'à 65535, ou `unsigned long` au-delà. Une multiplication
entre deux `int` déborde avant une éventuelle affectation à un `long` :

```c
unsigned long pixels = 320UL * 200UL;
unsigned long octets = pixels / 2UL;
```

Le suffixe et le type des opérandes doivent être choisis avant le calcul.
Les conversions et promotions restent celles du modèle documenté dans ABI.md.

## Écran ST standard

| Mode | Résolution | Plans | Taille de l'image |
|---|---|---|---|
| Basse résolution | 320 × 200, 16 couleurs | 4 | 32000 octets |
| Moyenne résolution | 640 × 200, 4 couleurs | 2 | 32000 octets |
| Haute résolution monochrome | 640 × 400, 2 couleurs | 1 | 32000 octets |

Le mode couleur standard comporte 200 lignes, pas 240. Le framebuffer est
planaire : en basse résolution, quatre mots successifs portent les quatre
plans d'un groupe de 16 pixels. Ce n'est pas un tableau de pixels rangés en
nibbles successifs. Les modes hors bordures demandent un autre contrat vidéo.

Un `int` signé suffit pour les offsets d'octets de 0 à 31999, mais pas pour un
indice linéaire allant jusqu'au pixel 63999. Deux écrans occupent 64000 octets.
Le placement de l'écran physique demande aussi l'alignement matériel du ST.

## GEMDOS, BIOS et XBIOS

Ces interfaces ont des types par paramètre, indépendamment de la largeur du
compilateur C employé. Les noms historiques se traduisent ainsi dans stcc :

| Type de la documentation | Type C stcc | Octets |
|---|---|---|
| `BYTE` / `UBYTE` | `signed char` / `unsigned char` | 1 en mémoire |
| `WORD` / `UWORD` | `short` ou `int` / leurs formes unsigned | 2 |
| `LONG` / `ULONG` | `long` / `unsigned long` | 4 |
| adresse, `VOIDP`, pointeur de fonction | type pointeur approprié | 4 |

Le numéro de fonction est un mot de 16 bits. Les arguments sont empilés en
ordre inverse, avec la largeur indiquée par l'appel, et le résultat revient
dans D0. Un paramètre décrit comme caractère peut néanmoins être un `WORD`
dans la convention système ; ne pas déduire sa case d'argument de son usage.

Quelques signatures représentatives, à fournir dans ses propres bindings :

```c
long Fread(int handle, long count, void *buffer);           /* GEMDOS */
long Fwrite(int handle, long count, const void *buffer);    /* GEMDOS */
long Bconin(int device);                                   /* BIOS */
void *Physbase(void);                                      /* XBIOS */
void Setscreen(void *logical, void *physical, int mode);    /* XBIOS */
```

Le piège est une fonction non déclarée, un prototype incorrect ou un wrapper
variadique appelé avec une valeur de 16 bits à la place d'un long. stcc accepte
les déclarations implicites historiques : sans prototype, le retour supposé
est `int`, ce qui tronquerait une adresse ou un résultat long. Avec un prototype
correct, la conversion des arguments est réalisée avant l'appel du wrapper C.

Un binding ne se réduit pas au prototype : il doit aussi construire le cadre
du TRAP, capturer D0 dans une variable C et respecter l'ABI de l'appelant.
Les exemples 5 à 8 montrent cette démarche sans bibliothèque externe.
BIOS/XBIOS peuvent modifier D0-D2/A0-A2. Un wrapper C ordinaire avec le bloc
inline protégé rétablit les non-volatils de stcc ; un registre explicitement
exposé au bloc doit être préservé par l'auteur si nécessaire. Voir REGISTERS.md.

Les règles par appel sont documentées dans *The Atari Compendium*, chapitres
GEMDOS, BIOS et XBIOS. Le tableau vidéo vient de la spécification matérielle
Atari ST. Les liens des sources sont réunis dans REFERENCES.md.
