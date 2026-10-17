# Assembleur Devpac intégré

Le modèle syntaxique est HiSoft Devpac 3 / GenST3 pour **68000 original**.
Ce n'est pas un remplacement exhaustif de GenST3 ni un lecteur d'objets GST.

## Source

Les mnémoniques et registres sont insensibles à la casse, les symboles sensibles
à la casse. Un label commence en colonne 1, ou se termine par `:` ailleurs.
Une instruction sans label doit être indentée. Les labels `.locaux` sont
attachés au dernier label non local du module. Les labels numériques `1$`
et les modes `OPT LOCALU/NOCASE` ne sont pas encore implémentés.

`;` commence un commentaire ; une ligne dont le premier caractère significatif
est `*` est un commentaire. Le premier espace hors chaîne dans le champ
opérande ouvre aussi le commentaire Devpac. Ne pas mettre d'espaces dans une
expression ni après les virgules : `move.l 4(a6),d0 commentaire` est accepté.

Constantes décimales, `$hex`, `%binaire`, `@octal`, chaînes entre `'` ou `"`
avec doublement du délimiteur. `*` dans une expression désigne le compteur
au début de l'instruction/directive.

Les expressions respectent l'ordre Devpac, différent de celui de C :

| Du plus prioritaire au moins prioritaire | Opérateurs |
|---|---|
| Unaires | `- + ~` |
| Décalages logiques | `<< >>` |
| Opérations bit à bit | `& ! ^` (`|` est un synonyme de `!`) |
| Produit / quotient | `* /` (`%` modulo est aussi accepté) |
| Somme / différence | `+ -` |
| Comparaisons signées | `= <> < > <= >=` (`==`, `!=` sont aussi acceptés) |

Les comparaisons produisent zéro ou `$ffffffff`, les résultats arithmétiques
absolus se replient sur 32 bits. Les opérateurs d'un même niveau sont associés
à gauche. Les expressions d'adresse sont conservées jusqu'à la liaison ; les
opérations non représentables par une relocation sont rejetées.

## Modes et instructions

Registres `d0-d7/a0-a7/sp`, indirect, pré-décrément, post-incrément,
déplacement 16 bits, index bref 8 bits avec registre `.w/.l`, absolu `.w/.l`,
PC-relatif et PC indexé, immédiat. Un symbole `label(pc)` est résolu relativement
au mot d'extension ; un nombre absolu `0(pc)` est un déplacement littéral.
Les index à échelle, extensions complètes, instructions 68010/68020 et FPU
sont rejetés. Un absolu sans suffixe utilise 32 bits, sans relaxation implicite.
Un absolu `.w` est étendu avec son signe par le processeur.

Les familles 68000 suivantes ont un encodeur :

```text
ABCD ADD ADDA ADDI ADDQ ADDX AND ANDI ASL ASR
Bcc BCHG BCLR BRA BSET BSR BTST CHK CLR CMP CMPA CMPI CMPM
DBcc DIVS DIVU EOR EORI EXG EXT ILLEGAL JMP JSR LEA LINK
LSL LSR MOVE MOVEA MOVEM MOVEP MOVEQ MULS MULU NBCD NEG NEGX
NOP NOT OR ORI PEA RESET ROL ROR ROXL ROXR RTE RTR RTS
SBCD Scc STOP SUB SUBA SUBI SUBQ SUBX SWAP TAS TRAP TRAPV TST UNLK
```

`MOVE` vers SR, CCR et USP et depuis SR/USP est accepté selon les possibilités
du 68000. `MOVE` **depuis CCR**, `BKPT`, `MOVEC`, `RTD` ne font pas partie du
68000 et ne sont pas acceptés. L'assemblage n'autorise pas leur exécution en
mode utilisateur : une instruction privilégiée reste privilégiée.

La présence d'un encodeur ne signifie pas que chaque combinaison a été exécutée
sur une machine réelle. Les tests couvrent les mots machine de référence,
les modes utilisés par le compilateur et plusieurs routines autonomes ; la
matrice exhaustive de toutes les instructions reste un travail complémentaire.

## Directives

| Directive | Support |
|---|---|
| `DC.B/.W/.L` | constantes et chaînes `.B` |
| `DS.B/.W/.L` | réserve, BSS sans octets dans le fichier |
| `DCB.B/.W/.L` | répétition d'une constante |
| `EQU` | constantes et alias d'adresse, alias symboliques différés |
| `EVEN`, `CNOP offset,alignement` | alignement de section |
| `SECTION text/data/bss` | trois sections fusionnées par type |
| `SECTION nom,CODE/DATA/BSS` | nom accepté, type choisissant la section |
| `TEXT`, `DATA`, `BSS` | raccourcis de section |
| `XDEF`, `XREF` | visibilité externe / import |
| `INCLUDE`, `INCBIN` | fichier relatif au module courant |
| `nom MACRO` ... `ENDM` | paramètres `\1..\9`, suffixe `\0`, identifiant `\@` |
| `REPT nombre` ... `ENDR` | répétition, y compris blocs imbriqués |
| `MACHINE 68000` | accepte cette cible et rejette les autres |
| `END` | fin du fichier |

Comme GenST3, les instructions et directives à éléments `.W/.L` s'alignent
sur un mot avant d'associer leur label. Une ligne contenant seulement un label
conserve l'adresse courante, même si elle est impaire.

`ORG`, `SET`, `REG`, assemblage conditionnel, paramètres macros nommés,
`MEXIT`, `NARG`, fichiers WITH, options OPT, listings GenST3 et autres formats
de sortie ne sont pas encore disponibles. Ils ne sont pas ignorés silencieusement.
Les macros acceptent jusqu'à neuf paramètres et une profondeur limitée ; il
n'y a pas de gestion des extensions complètes de macros GenST3.
