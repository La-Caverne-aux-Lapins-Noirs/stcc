# Variables register et choix de pile

## Placement automatique ou registre imposé

```c
register int compteur = 0;
register(d3) unsigned long ticks = 0UL;
register(a3) unsigned short *curseur = ecran;
```

`register` est le mot-clé C89 : stcc essaie maintenant de placer les variables
locales et les paramètres scalaires dans un registre. Les entiers utilisent
D2 à D7 ; les pointeurs préfèrent A2 à A5, puis les registres de données libres.
Lorsque les registres disponibles sont épuisés, le placement automatique
utilise la pile. Le mot-clé reste une indication, sans garantie de placement.

`register(registre)` est une extension stcc avec une garantie plus forte :
la variable occupe effectivement ce registre pendant sa portée lexicale.
La casse du nom de registre est indifférente ; aucun guillemet n'est requis.
Les macros C fonctionnent dans cette déclaration.

| Registre imposé | Types acceptés |
|---|---|
| D2 à D7 | types entiers disponibles, enums et pointeurs |
| A2 à A5 | pointeurs ou entiers de 32 bits (`long`, `unsigned long`) |
| D0/D1, A0/A1 | réservés aux calculs intermédiaires et à l'ABI |
| A6 | réservé au cadre de fonction C |
| A7 / SP | réservé à la pile ; voir les mécanismes ci-dessous |

Le placement est local à une fonction, y compris pour ses paramètres. Il ne
définit pas une variable globale en registre ni une nouvelle convention
d'appel. Les paramètres sont toujours transmis sur la pile, puis copiés dans
les registres choisis au début de la fonction. Le résultat reste dans D0.

Les registres explicitement demandés dans la fonction sont réservés avant
le placement automatique. Deux variables actives ne peuvent pas demander
le même registre. Deux blocs de portées disjointes peuvent le réutiliser.
Un registre imposé indisponible provoque une erreur, sans repli silencieux
vers la mémoire. Les tableaux et agrégats ne tiennent pas dans un registre
imposé ; un simple `register` sur ces objets conserve leur stockage mémoire.

L'adresse d'une variable déclarée `register` ne peut pas être prise, même
en cas de repli sur la pile. `sizeof` reste utilisable. Pour représenter le
contenu entier d'un registre de données, employer `unsigned long` : un `int`
ne représente que les 16 bits de sa valeur C. Les lectures et écritures C
respectent les conversions, promotions et extensions habituelles du type.

## Appels, interruptions et assembleur inline

Une fonction C sauvegarde à son entrée les registres non volatils qu'elle
utilise, puis les restaure à son retour. L'appelant conserve donc ses variables
en registre à travers les appels C, les appels récursifs et les primitives
arithmétiques. Une routine assembleur appelée doit respecter la même ABI.
`__interrupt__` conserve sa sauvegarde complète et son retour RTE : ses
variables locales en registre ne détruisent pas celles du code interrompu.

Dans l'assembleur inline, le nom C d'une variable placée en registre devient
ce registre, et non une adresse de stockage :

```c
register(d3) unsigned long ticks = 41UL;
__asm__ {
    addq.l  #1,ticks
}
/* ticks vaut maintenant 42. */
```

Avec un registre imposé, employer directement son nom matériel a le même
effet. Le compilateur ne restaure pas son ancienne valeur à la sortie du bloc.
Les variables en registre nommées dans un bloc sont également exposées en
lecture/écriture. Les autres registres non volatils restent protégés par le
MOVEM du bloc. Une variable automatiquement placée et absente des opérandes
conserve sa valeur même si le bloc utilise son registre comme temporaire.

Le corps assembleur reste sous la responsabilité de son auteur : un TRAP ou
un appel brut peut modifier les registres exposés. En particulier, la convention
BIOS autorise la modification de D0-D2/A0-A2. Employer une fonction C enveloppe
respectant l'ABI ou préserver soi-même un registre imposé qui doit survivre.
La déclaration `register` ne change pas les effets matériels d'un TRAP.

Les listes de clobbers ne sont pas des contraintes d'entrée/sortie GCC.
Ces garanties de stockage sont propres à stcc ; cette syntaxe ne promet pas
les mêmes règles que les variables de registre explicites de GCC.

## Choisir la pile au démarrage

La pile de 16 Kio est désormais une valeur par défaut :

```sh
./stcc --stack-size 32768 main.c -o DEMO.PRG
./stcc --rom --stack-size 32768 noyau.c -o NOYAU.IMG
```

La taille est exprimée en octets, en décimal ou en hexadécimal `0x...`. Elle
doit être paire et comprise entre 256 et 16 777 216 octets. La mémoire réelle
disponible reste déterminante ; le profil ROM vérifie aussi sa limite RAM.
Le linker réserve la pile à la fin du BSS, et le démarrage la met à zéro.
Il n'existe pas encore de garde contre les débordements, ni d'analyse de la
profondeur des appels ou des interruptions.

Pour fournir sa propre zone, déclarer par exemple une variable globale :

```c
unsigned long ma_pile[8192]; /* 32 Kio, alignés sur un mot. */
```

```sh
./stcc --stack-top '_ma_pile+32768' main.c -o DEMO.PRG --map DEMO.map
```

`--stack-top` prend une expression du linker. Les noms C externes reçoivent
le préfixe `_` ; un nom exporté par un fichier `.s` est utilisable directement.
Cette option remplace la réserve de pile automatique : on ne réserve pas
16 Kio supplémentaires. Le sommet doit être pair et dans la mémoire accessible
en écriture. Il appartient au programme de réserver une zone suffisante sous
ce sommet et de ne pas la faire chevaucher d'autres données.

Pour un PRG, le sommet doit appartenir à DATA/BSS et se relocaliser avec le
programme. Le démarrage calcule la taille à conserver par `Mshrink` à partir
de `__stcc_program_end`, et non à partir de SP : les variables situées après
la pile choisie sont ainsi conservées. Le sommet effectif est exporté sous
`__stcc_stack_end` dans la carte, y compris lorsqu'il s'agit d'un alias.

Pour une ROM autonome, un sommet numérique dans l'arène RAM est aussi permis :

```sh
./stcc --rom --stack-top 0x70000 noyau.c -o NOYAU.IMG
```

Il faut alors organiser soi-même la zone sous ce sommet. `--stack-top` et
`--stack-size` s'excluent et nécessitent le démarrage automatique. Avec
`--nostartfiles`, le programme fournit son initialisation ; `runtime/integer.s`
contient seulement les primitives arithmétiques et `runtime/stack.s` permet
de réintroduire séparément une pile standard de 16 Kio.

## Changer de pile en cours d'exécution

Le 68000 permet de déplacer SP. Les appels, adresses de retour et temporaires
empilés exigent cependant que l'ancienne pile soit retrouvée au bon moment.
Une affectation à SP dans une expression C ordinaire casserait ces hypothèses.

L'exemple `examples/17_stack.c` fournit un trampoline `__naked__` : il mémorise
SP, choisit une autre pile, appelle une fonction C, restaure SP puis fait RTS.
La fonction C peut alors utiliser normalement ses variables locales et appeler
d'autres fonctions. Cet exemple emploie une sauvegarde globale unique et n'est
donc pas réentrant ; un système de coroutines doit stocker un contexte séparé
pour chaque tâche.

Sur un PRG lancé normalement en mode utilisateur, ce réglage concerne sa pile
utilisateur. Les exceptions utilisent la pile superviseur gérée par TOS. Dans
notre ROM, `main` s'exécute en superviseur : la pile choisie sert également aux
exceptions qui surviennent dans ce mode.

## Vérifications

```sh
make test
make examples
python3 tests/registers_hatari.py --hatari hatari
python3 tests/registers_hatari.py --hatari hatari --tos /chemin/etos192us.img
```

La suite CPU vérifie les types, les mises à jour, les pointeurs, la récursion,
les dix registres disponibles à travers un appel, la pression de registres,
l'inline et les variantes de pile. La suite Hatari vérifie aussi TRAP/RTE avec
des variables en registre, une pile fixe en ROM et les PRG avec TOS fourni.
