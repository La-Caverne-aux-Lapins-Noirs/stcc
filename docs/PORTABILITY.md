# Macros de cible et code commun

Ces macros sont définies avant la lecture d'une source C, sans en-tête à inclure.
Elles décrivent la cible 68000, indépendamment du système Linux qui exécute stcc.
Elles sont aussi disponibles avec `-E`, `-S` et `-c`.

| Macro | Valeur dans stcc 0.6.0 |
|---|---|
| `__STCC__` | `1` ; identification du compilateur, conservée des versions précédentes |
| `__STCC_VERSION__` | `600L` : majeur × 10000 + mineur × 100 + correctif |
| `__STCC_VERSION_MAJOR__` / `__STCC_VERSION_MINOR__` / `__STCC_VERSION_PATCH__` | `0` / `6` / `0` |
| `__STCC_VERSION_STRING__` | `"0.6.0"` |
| `__ATARI_ST__` | `1` ; matériel visé, y compris dans une ROM autonome |
| `__M68000__` | `1` ; jeu d'instructions du 68000 original |
| `__CHAR_BIT__` | `8` |
| `__SIZEOF_SHORT__` / `__SIZEOF_INT__` | `2` / `2` octets |
| `__SIZEOF_LONG__` / `__SIZEOF_POINTER__` | `4` / `4` octets |
| `__ORDER_LITTLE_ENDIAN__` / `__ORDER_BIG_ENDIAN__` | `1234` / `4321` |
| `__BYTE_ORDER__` | `__ORDER_BIG_ENDIAN__` |
| `__STDC_HOSTED__` | `0` ; environnement autonome |

`__FILE__` et `__LINE__` restent disponibles dans les expressions C.
Les valeurs sont des macros numériques, sauf la chaîne de version.

```c
#if defined(__ATARI_ST__)
/* Binding Atari, mémoire vidéo planaire, appels TOS si le système est présent. */
#else
/* Backend PC, SDL ou autre environnement du projet. */
#endif

#if defined(__STCC__) && __STCC_VERSION__ >= 600L
/* Fonctionnalité de cette livraison. */
#endif
```

`__ATARI_ST__` n'est pas une détection de TOS à l'exécution. La même cible peut
produire un PRG pour TOS ou une ROM sans système. Aucune macro `__TOS__` n'est
injectée à partir du mode de liaison : cela rendrait les unités compilées avec
`-c` ambiguës. Un projet peut choisir `-D MON_PROJET_AVEC_TOS=1` pour ses bindings.

Ne pas utiliser la taille de `long` du PC comme promesse de portabilité : il
fait souvent 64 bits sur Linux amd64, contre 32 sur stcc. Les macros aident à
choisir un backend et à vérifier des hypothèses ; elles ne rendent pas des
objets STCO compatibles avec les objets du compilateur hôte.

Les `-D` utilisateur et les directives ordinaires peuvent redéfinir ces noms.
Cela ne change ni la représentation réelle des types, ni l'ABI, ni la machine
ciblée. Ne pas annoncer de conformité C89 complète en se fondant sur une macro.

Voir `examples/19_portable.c` : calcul du nombre d'octets d'un écran, avec le
même résultat sous stcc et sous `cc -std=c89` sur le PC. Voir TOSCALLS.md pour
les conversions d'entiers, les prototypes et les dimensions réelles de l'écran.
