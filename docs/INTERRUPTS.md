# Entrées d'interruption et routines assembleur

## Adresse du code et vecteur

Sur le 68000 du ST, la table des vecteurs contient des pointeurs de 32 bits.
Le vecteur d'une interruption donne l'adresse de sa routine ; le code n'a pas
à résider dans la table. Par exemple, la VBL matérielle utilise le vecteur 28
dont le pointeur se trouve à `$70`. Le numéro de vecteur et l'adresse de sa
case sont deux choses différentes.

Un PRG peut rester relocalisable. Une fois chargé, son programme installe
l'adresse effective d'une fonction, par exemple avec BIOS `Setexc`. Cette
version ne fournit ni `ORG` ni un attribut imposant une adresse de chargement.
Les deux spécificateurs ci-dessous définissent l'entrée et la sortie du code.

## Gestionnaire C : `__interrupt__`

```c
volatile unsigned int ticks;

__interrupt__ void tick(void)
{
    ticks++;
}
```

Le compilateur émet, dans cet ordre :

```asm
_tick:
        movem.l d0-d7/a0-a6,-(sp)
        link a6,#-taille_des_locales
        ; corps C et branches de return
retour:
        unlk a6
        movem.l (sp)+,d0-d7/a0-a6
        rte
```

Le processeur a déjà empilé SR et PC sur sa pile superviseur. `RTE` restaure
ce contexte ; `RTS` attend seulement une adresse de retour de sous-programme.
La sauvegarde ajoutée occupe 60 octets, auxquels s'ajoutent le cadre `a6`, les
variables locales, les temporaires et les appels imbriqués. Le gestionnaire
emploie la pile superviseur existante, sans changement automatique de pile.

Le corps peut utiliser les constructions C prises en charge et appeler les
fonctions C ordinaires de stcc. `return;` et la fin du corps passent par le
même épilogue. L'installation du vecteur, le masquage éventuel des interruptions,
l'acquittement du périphérique et la gestion des données partagées restent
explicites. `volatile` assure les accès mémoire ; il ne rend pas un calcul
composé atomique. Les routines GEMDOS ne s'appellent pas depuis un gestionnaire.

Ce mode vise les entrées à trame courte du **68000 original** : notamment VBL,
interruptions MFP et exceptions TRAP choisies par l'application. Les erreurs
bus/adresse possèdent une autre trame ; les vecteurs de reset ont encore un
autre contrat. Ils demandent une entrée assembleur adaptée. Pour le MFP, suivre
aussi le protocole d'acquittement et le mode d'EOI effectivement installés.

Le type est obligatoirement `void (void)`, avec prototype explicite, sans
paramètre ni ellipsis. Répéter le spécificateur sur les prototypes et la
définition. Il fait partie du type : un appel C direct ou indirect à un
`__interrupt__` est rejeté. On peut déclarer un pointeur adapté :

```c
typedef __interrupt__ void (*Irq)(void);
```

Une conversion explicite peut contourner le contrôle de type, mais ne change
pas l'instruction de retour de la routine.

## Entrée entièrement écrite à la main : `__naked__`

```c
__interrupt__ __naked__ void entry(void)
{
    __asm__ {
        movem.l d0-d7/a0-a6,-(sp)
        ; travail assembleur
        movem.l (sp)+,d0-d7/a0-a6
        rte
    }
}
```

`__naked__` ne génère **aucune instruction** en dehors des blocs assembleur :
ni `LINK`, ni sauvegarde automatique, ni initialisation de résultat, ni retour.
Sa signature est également `void (void)`. Le corps accepte les blocs `__asm__`,
les blocs de regroupement et les instructions vides ; les déclarations et
instructions C sont rejetées. La liste de clobbers est rejetée car elle ne
déclencherait aucune protection.

Les noms C globaux et les labels locaux sont toujours résolus. Il n'y a pas
de cadre de paramètres ou de variables locales accessible par `a6`. Le code
assembleur choisit ses sauvegardes, sa pile et tous ses chemins de sortie.
Un `__naked__` seul peut aussi fournir une routine ordinaire terminant par
`RTS`, à condition de respecter l'ABI de son appelant.

Combiner `__interrupt__` et `__naked__` conserve la distinction de type, mais
**supprime toutes les instructions automatiques**, y compris `RTE`.
Les spécificateurs s'écrivent avant le déclarateur et peuvent qualifier un
typedef de pointeur de fonction. Ils doivent concorder entre déclarations.
Ils ne s'appliquent pas à `main`.

## Choisir le bon contrat Atari

| Point d'entrée | Forme à employer |
|---|---|
| Vecteur CPU direct, trame courte 68000 | `__interrupt__ void f(void)` |
| Callback de la file VBL différée de TOS | fonction C ordinaire `void f(void)` ; retour `RTS` |
| Chaînage, contrat de registres particulier, autre trame | `__naked__` ou module `.s`, contrat écrit explicitement |

Le chaînage d'un gestionnaire qui attend une trame d'exception est un transfert
de contrôle avec cette trame intacte. Un appel C, qui empile une adresse de
retour ordinaire, ne convient pas. Une extension résidente doit aussi prévoir
sa durée de vie mémoire, son retrait et le protocole XBRA. Les exemples sont
temporaires et retirent leurs propres entrées avant `Pterm`.

Les exemples exécutables fournis sont :

- `12_interrupts.c` : installation via `Setexc`, TRAP #3 et #4 imbriqués,
  calcul C et retour `RTE` ; vérifie les quinze registres, SR et SP.
- `13_naked_chain.c` : entrée brute qui transfère le contrôle à son prédécesseur.
- `14_vbl.c` : callback C ordinaire dans une case libre de la file VBL TOS,
  installation/retrait sous `Supexec`, écriture protégée par le masque IPL,
  attente de cinq VBL réelles avec `Vsync`. La file et ses autres entrées restent
  en place ; le callback ne fait aucun appel système.

Ces essais passent sous Hatari ST/M68000 et EmuTOS 1.4. Ils ne valident pas
tous les périphériques MFP, les autres processeurs 68k ou toutes les ROM TOS.
Les références matérielles et système figurent dans `REFERENCES.md`.
