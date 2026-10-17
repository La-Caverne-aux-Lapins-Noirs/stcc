# Des symboles au programme TOS ou à la ROM

## STCO v1

STCO est un format propre à stcc, sans dépendance à la taille des structures C
hôtes. Tous les nombres de contrôle sont des entiers non signés big-endian
sur 32 bits. Les chaînes sont une longueur 32 bits puis leurs octets sans NUL.

Le fichier contient dans cet ordre :

1. huit octets `53 54 43 4f 00 00 00 01` (`STCO`, version 1) ;
2. longueur TEXT, octets TEXT ;
3. longueur DATA, octets DATA ;
4. longueur BSS, sans octets de contenu ;
5. nombre de symboles, puis leurs enregistrements ;
6. nombre de fixups, puis leurs enregistrements.

Un symbole contient : nom, section codée, déplacement dans la section,
indicateur exporté/importé, chaîne d'alias `EQU` (vide hors alias).

| Code section stocké | Signification |
|---|---|
| 0 | alias EQU, expression différée |
| 1 | symbole encore indéfini |
| 2 | TEXT |
| 3 | DATA |
| 4 | BSS |

Un fixup contient : section (TEXT=0, DATA=1), déplacement de l'octet à corriger,
largeur (1/2/4), mode relatif, déplacement PC de référence, contrôle de plage,
expression, fichier d'origine et numéro de ligne.

Modes relatifs : 0 pour valeur absolue, 1 pour destination de branche,
2 pour adressage PC. Pour le mode 2, un symbole relocalisable devient un
déplacement PC ; une expression absolue est déjà un déplacement numérique.
Le contrôle de plage distingue les mots signés, les absolus courts signés,
les branches courtes non nulles et les immédiats byte stockés dans un mot.

Les expressions sont déjà débarrassées de leur portée implicite : `.boucle`
est un nom canonique attaché à son label non local, et `*` est remplacé par
un symbole interne qui mémorise l'adresse de la ligne.

## Liaison

L'éditeur de liens choisit d'abord les positions des contributions TEXT, DATA
et BSS. À l'intérieur d'un objet, un nom défini localement est prioritaire.
Sinon le linker cherche une définition exportée et unique dans les autres
objets. Il rejette les doublons, les références indéfinies, les cycles d'alias,
les dépassements de déplacement et les relocations non représentables.

Une valeur d'expression porte une composante numérique et le coefficient de
l'adresse de chargement. `label+4` a un coefficient de un : le chargeur doit
y ajouter son adresse réelle. `label2-label1` a un coefficient de zéro.
`label1+label2` aurait un coefficient de deux et n'est pas une relocation TOS.

TOS ne sait relocaliser que des **longwords absolus** à des emplacements pairs.
Un mot contenant une adresse dépendante du chargement est donc rejeté. Une
branche calcule directement sa différence et n'entre pas dans la liste TOS.

## Fichier TOS

Le programme comporte l'en-tête de 28 octets, le TEXT, le DATA puis la chaîne
de relocations. Le BSS est réservé au chargement et n'occupe pas le fichier.
Les longueurs et les déplacements ne supposent jamais un chargement fixe.

La table des relocations commence par un long donnant le premier emplacement
à corriger. Chaque octet suivant indique une distance paire vers le suivant.
Un octet 1 avance de 254 sans effectuer de correction ; zéro termine la liste.
Un long initial nul représente une liste vide.

`--map` donne les adresses relatives à l'origine du TEXT. Pour une comparaison
avec le debugger, ajouter l'adresse réelle du TEXT allouée par TOS. Le format
de cette carte n'est pas directement celui des symboles DRI ni un format de
symboles de débogage normalisé.

## Image ROM

Le mode `--rom` utilise les mêmes objets STCO v1 et règles de résolution.
Il affecte à TEXT une adresse ROM fixe, à DATA une adresse RAM fixe et à BSS
la RAM qui suit DATA. Les valeurs des fixups sont liées à ces adresses finales.
Le coefficient d'adresse sert encore à distinguer les cibles PC-relatives
des déplacements numériques ; aucune relocation TOS n'est écrite.

Le fichier contient TEXT puis l'image initiale de DATA, suivis de `$FF`
jusqu'à la capacité demandée. Le démarrage copie cette image DATA en RAM.
Le BSS n'occupe pas le fichier. `--map` indique les adresses absolues et les
symboles réservés du linker décrits dans [ROM.md](ROM.md). L'en-tête de ROM
appartient au module de démarrage, et non au format objet ou au linker brut.
