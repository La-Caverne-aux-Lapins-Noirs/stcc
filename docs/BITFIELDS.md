# Champs de bits

```c
struct Packet {
    unsigned int ready : 1;
    unsigned int channel : 3;
    signed int delta : 4;
    unsigned int value : 8;
};
```

Le membre `ready` occupe le bit 15, `channel` les bits 14..12, `delta`
les bits 11..8 et `value` les bits 7..0. `sizeof(struct Packet)` vaut 2.
L'exemple `11_bits.c` vérifie aussi les octets de cette représentation.

La disposition des champs de bits dépend de l'implémentation du C.
Voici le contrat de stcc, sans garantie de compatibilité avec un autre ABI :

- Types de base : `int`, `signed int`, `unsigned int`, leurs typedefs et
  qualifications `const`/`volatile`. Un `int` sans signedness explicite est signé.
- Largeurs de 1 à 16 bits pour un membre nommé. Chaque unité de stockage est
  un mot de 16 bits aligné sur deux octets, rempli du bit fort vers le bit faible.
- Un champ qui ne tient pas commence un nouveau mot ; il ne chevauche jamais
  deux unités. Un membre ordinaire ferme l'unité de bits précédente.
- Un champ sans nom réserve des bits sans participer à l'initialisation.
  Un champ sans nom de largeur zéro ferme l'unité courante et aligne la suite
  sur le prochain mot, sans réserver un mot vide supplémentaire.
- Dans une union, chaque champ part du bit fort du premier mot ; la taille
  reste le maximum des membres, arrondi à deux octets.

Les valeurs signées sont étendues avec leur signe. Une écriture conserve les
bits faibles de la valeur convertie à la largeur du champ ; les bits voisins
restent inchangés. `=`, les affectations composées, `++` et `--` sont pris en
charge et évaluent une seule fois l'adresse de leur opérande gauche.

La promotion d'un champ unsigned de moins de 16 bits produit un `int` signé,
qui peut représenter toutes ses valeurs. Un champ unsigned de 16 bits reste
unsigned ; les champs signés sont promus en `int` signé.

Les initialisations globales, statiques et automatiques utilisent la même
disposition. Les membres omis sont nuls ; une copie de structure conserve
l'ensemble des octets. Les champs sans nom ne consomment aucun élément de la
liste d'initialisation. Comme ailleurs, les agrégats imbriqués demandent leurs
accolades explicites.

`&objet.champ` et `sizeof(objet.champ)` sont rejetés. Un champ de bits n'a pas
d'adresse C propre ; `sizeof(+objet.champ)` porte sur l'entier promu.

Un accès à un champ volatile lit son mot de stockage. Une écriture lit,
masque et réécrit ce mot ; ce n'est pas une opération atomique. Pour un
registre matériel ayant des effets à la lecture ou à l'écriture, employer
les accès explicites prévus par sa documentation.
