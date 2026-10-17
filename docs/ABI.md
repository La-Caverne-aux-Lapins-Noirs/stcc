# ABI 68000 et démarrage TOS

## Objets et représentation

Octets de 8 bits, big-endian ; `char` signé, `short/int/enum` de 16 bits,
`long` et pointeurs de 32 bits. Les unsigned utilisent les mêmes largeurs.
Les représentations entières signées sont en complément à deux. La conversion
vers un entier signé plus petit conserve les bits faibles ; le décalage signé
à droite propage le signe. Le débordement arithmétique signé reste un
comportement indéfini du langage C.

Le padding d'une structure précède chaque membre qui en a besoin. Les membres
longs et les pointeurs demandent deux octets d'alignement, pas quatre. Toutes
les structures et unions sont alignées sur deux octets et leur taille est paire.
Un tableau est contigu, sans padding supplémentaire entre ses éléments.
Les champs de bits utilisent des mots de 16 bits, du bit fort vers le bit
faible, sans chevauchement de mots : voir [BITFIELDS.md](BITFIELDS.md).

## Appel de fonction

Le nom externe C `f` est le symbole assembleur `_f`. Les fonctions `static`
n'exportent pas ce symbole ; leurs noms restent locaux à l'objet STCO.

L'appelant évalue et empile les arguments de droite à gauche. Un argument de
1 ou 2 octets occupe une case de 2 octets ; un argument de 4 octets occupe
4 octets. Les petites valeurs entières ont subi les promotions ordinaires
ou la conversion vers le type du prototype avant leur placement.

À l'entrée d'une fonction avant son prologue :

| Adresse | Contenu |
|---|---|
| `0(sp)` | adresse de retour, 4 octets |
| `4(sp)` | premier argument |
| suite | arguments suivants, sans trou additionnel |

Le prologue est `LINK a6,#-taille`. Après celui-ci :

| Adresse | Contenu |
|---|---|
| `0(a6)` | ancien `a6`, 4 octets |
| `4(a6)` | adresse de retour |
| `8(a6)` | première case d'argument |
| déplacements négatifs | variables locales |

Le char d'un paramètre prototypé occupe l'octet bas de sa case : `9(a6)` si
c'est le premier paramètre. Un paramètre de type `int` commence à `8(a6)`.
Le prochain paramètre commence après la **case**, pas après le seul char.

Les résultats scalaires reviennent dans `d0`. Le compilateur normalise les
bits hauts à leur retour : extension du signe pour un entier signé, zéros pour
un unsigned. Une routine assembleur retournant un `int` peut donc ne définir
que `d0.w` ; une routine retournant un `long` ou un pointeur définit tout `d0`.

L'appelant nettoie les arguments. Le retour est `UNLK a6` puis `RTS`.
`d0/d1/a0/a1` et CCR sont volatils. `d2-d7/a2-a6` doivent être préservés.
Les routines arithmétiques internes emploient une ABI de registres privée,
décrite dans `runtime/integer.s`, et préservent les registres non volatils.

Le passage et le retour de structures/unions par valeur sont rejetés.
Passer leurs adresses est pris en charge. Les fonctions variadiques existent ;
`stdarg.h` suit cette pile. Il faut demander à `va_arg` le type promu (`int`,
`unsigned int`, `long`, pointeur), et choisir un dernier paramètre nommé
compatible avec ses promotions, comme l'exige le contrat de `va_start`.

## Conventions d'entrée spéciales

`__interrupt__ void f(void)` sauvegarde `d0-d7/a0-a6` avant son `LINK`,
les restaure après `UNLK`, puis effectue `RTE`. `__naked__ void f(void)`
n'ajoute aucune instruction. Les deux attributs participent à la compatibilité
des types de fonction. Voir [INTERRUPTS.md](INTERRUPTS.md) pour les trames CPU,
les callbacks TOS et le chaînage.

## Exécutable et démarrage

La liaison place successivement tous les TEXT, tous les DATA puis tous les
BSS, en alignant chaque contribution sur deux octets. Le premier objet est
le démarrage. Le dernier objet runtime réserve **16 Kio de pile au-dessus de
tous les BSS utilisateur**.

TOS entre au premier octet de TEXT avec la basepage à `4(sp)`.
`runtime/start.s` :

1. récupère cette adresse et efface explicitement tout le BSS ;
2. la conserve dans `__stcc_basepage`, également exportée sous `_stcc_basepage` ;
3. place `sp` en haut de la pile réservée ;
4. appelle GEMDOS `Mshrink` avec la taille réellement nécessaire ;
5. appelle `_main`, sans argument ;
6. transmet `d0.w` à GEMDOS `Pterm` (`$4c`).

L'en-tête `<stcc.h>` expose `stcc_basepage` en C. Il donne accès aux dimensions
des segments, à l'environnement et à la ligne de commande brute de TOS.
Il n'y a pas encore de conversion en `argc/argv`, de gestion de `ARGV`,
de vérification de débordement de pile ni de support des desk accessories.

L'en-tête TOS fait 28 octets, commence par `$601a`, indique les trois tailles,
une table de symboles de taille zéro et des flags zéro. `ABSFLAG` vaut zéro.
Les longwords contenant une adresse chargeable figurent dans la chaîne de
relocalisations TOS ; aucune adresse de chargement n'est imposée. Les
références PC-relatives ne demandent pas de relocation du chargeur.

Les branches C sont des déplacements signés de 16 bits ; une fonction qui
les dépasse est rejetée à la liaison. Le cadre local est limité à 32760 octets.
Les segments d'entrée sont bornés à 16 Mio. Les pointeurs physiques dépassant
l'espace adressable de 24 bits du ST ne deviennent pas utilisables parce que
le type pointeur a une représentation sur 32 bits.
