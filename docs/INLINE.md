# Assembleur inline Devpac

## Syntaxe et portée

```c
__asm__ {
    move.w  compteur,d0
    addq.w  #1,d0
    move.w  d0,compteur
}
```

Ou avec une liste de modifications :

```c
__asm__ (d0, d1, a0, a1, cc, memory) {
    move.l  message,-(sp)
    move.w  #9,-(sp)
    trap    #1
    addq.l  #6,sp
}
```

Le bloc est une instruction C ; un point-virgule final est accepté.
Son contenu est opaque au préprocesseur C : les macros C n'y sont pas
substituées. Il est analysé ensuite par l'assembleur, avec ses constantes,
commentaires et expressions Devpac.

Les identifiants C **dans les opérandes** sont résolus selon leur portée C :

- variable locale / paramètre : opérande mémoire `déplacement(a6)` ;
- variable globale ou statique : symbole assembleur de stockage ;
- fonction déclarée : symbole de fonction avec le préfixe `_` ;
- nom non reconnu : symbole assembleur, résolu par l'assembleur et le linker.

Les noms de registres désignent toujours les registres. Un identifiant C qui
porte le nom d'un registre demande un autre nom de variable pour son accès
inline dans cette version. Les noms des mnémoniques ne sont pas substitués.
Les chaînes, constantes numériques et commentaires ne le sont pas non plus.

Les labels internes se nomment `.boucle:`. Chaque bloc possède une portée
assembleur distincte : on peut réutiliser `.boucle:` dans un autre bloc.
Un label non local avec `:` dans un bloc inline est rejeté. Préférer les labels
avec `:` dans l'inline, même si les fichiers `.s` acceptent ceux de colonne 1.

La résolution d'un nom fournit une **opérande mémoire**, pas sa valeur
immédiate ni systématiquement son adresse :

```asm
        move.l  message,a0      ; lire la valeur d'un pointeur C
        lea     tableau,a0      ; obtenir l'adresse d'un tableau local
        move.w  2(a0),d0        ; accéder à un élément depuis cette adresse
```

`#locale`, `locale.membre`, ou l'ajout d'un déplacement après le nom d'une
variable locale ne sont pas des substitutions prises en charge. Utiliser `LEA`
puis les modes d'adressage normaux du 68000.

## Registres et mémoire

Sans liste, le compilateur suppose que `d0/d1/a0/a1`, CCR et la mémoire peuvent
être modifiés. Avec une liste, les noms acceptés sont `d0-d7`, `a0-a5`, `cc`
et `memory`. Les noms `sp`, `a7`, `a6` sont rejetés dans cette liste.

Dans cette version conservatrice, **tous** les blocs ordinaires sont des barrières mémoire
et de flags ; la liste ne sert pas à supprimer des protections. Les valeurs C
intermédiaires sont sur la pile, les variables C restent en mémoire, et aucun
accès n'est déplacé au travers du bloc. Il n'y a ni cache de variable en registre,
ni dépendance aux flags en entrée ou en sortie.

Dans une fonction ordinaire ou `__interrupt__`, chaque bloc sauvegarde `d2-d7/a2-a5` avec `MOVEM.L`, puis les restaure avant
l'instruction C suivante. C'est le cas même si la liste déclare leur modification.
Cela maintient la convention d'appel du code C environnant. Les variables sont
adressées via `a6`, indépendamment des arguments empilés par le bloc.

## Contrat du programme assembleur ordinaire

- `a6` doit conserver sa valeur durant tout le bloc. Ne pas utiliser `LINK`,
  `UNLK` ou un chargement de registre pour le modifier.
- `sp` doit être identique à sa valeur d'entrée à la sortie du bloc. Les `push`
  nécessaires à un `TRAP` sont autorisés si leurs `pop`/ajustements correspondent.
- La sauvegarde automatique des non-volatils occupe 40 octets supplémentaires
  à l'entrée du bloc. Ne pas chercher les paramètres C avec `sp`.
- Tous les chemins de contrôle doivent revenir à la fin du même bloc. Les
  branches vers ses labels locaux sont autorisées. Sortir par `RTS`, `RTE`,
  `JMP`/branche vers une étiquette C ou au milieu d'un autre bloc ne l'est pas.
- Les accès mémoire et tailles d'opérande restent sous la responsabilité de
  l'auteur : `move.l` sur une variable `int` de 16 bits écraserait son voisin.
- Les instructions privilégiées exigent le mode superviseur approprié.

Le compilateur vérifie la liste de clobbers et les encodages. Il ne prouve pas
l'équilibre de pile, les effets d'un calcul d'adresse ni les chemins d'un
programme assembleur arbitraire. Violer ce contrat invalide les garanties C.
Pour un contrôle complet de l'entrée et du retour, employer `__naked__`
ou un module `.s`. Dans `__naked__`, il n'y a ni cadre `a6`, ni sauvegarde
automatique, ni retour ajouté ; le corps n'accepte que l'assembleur.
Voir [INTERRUPTS.md](INTERRUPTS.md) pour ce contrat et les gestionnaires `RTE`.
