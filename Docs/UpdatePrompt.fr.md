# Prompt de mise à jour de CyMCPSDK

Copier le bloc ci-dessous dans l’assistant qui travaille sur l’application hôte. Préciser la révision cible ou indiquer le checkout local à utiliser.

```text
Mets à jour l’intégration existante de CyMCPSDK dans cette application. Identifie la révision réellement utilisée, passe à la révision cible, adapte le code et les configurations concernés, puis valide le résultat de bout en bout. Préserve les fonctionnalités, les données et les commandes déjà utilisées.

Utilise en priorité les sources de la copie locale de CyMCPSDK indiquée pour ce projet ou située dans ../CyMCPSDK. Sans cible explicite, utilise la révision de ce checkout. Récupère les sources depuis https://github.com/MrMybal/CyMCPSDK uniquement si la copie locale est indisponible. Compile le SDK depuis ses sources. Pour Unreal, consulte la copie locale de CyUEMCPSDK, par exemple ../CyUEMCPSDK, lorsque ses exemples concernent le mode d’intégration utilisé.

1. Identifie la version réellement intégrée

Repère la version et la révision épinglées dans le projet, les modules liés et la provenance des binaires effectivement chargés. Vérifie les références dans CMake ou les règles de build Unreal, les dépendances embarquées, les scripts de packaging et les exemples de configuration. Si la révision d’origine ne peut pas être établie, indique ce que tu as pu confirmer.

Compare les révisions de départ et d’arrivée à partir des notes de version, du code public et de la documentation. Identifie les changements qui touchent cette application, notamment les API, les valeurs par défaut, les formats de configuration, les protocoles, les dépendances et les données persistées.

2. Mets à jour la dépendance et adapte l’application

Mets à jour la référence à CyMCPSDK et reconstruis les modules utilisés par l’application. Vérifie que le produit final charge les nouveaux composants et que le packaging utilise la même révision que le build testé.

Adapte les appels, configurations et scripts concernés. Préserve les noms des opérations métier, leurs arguments et résultats publics, le comportement des scripts, les droits et les règles de confirmation. Si un changement de format est nécessaire, prévois une migration et vérifie-la sur des copies de données produites par l’ancienne version. Cela inclut les configurations, les historiques et les jobs persistés utilisés par le projet.

Vérifie que le chat, le CLI et le serveur MCP appellent les opérations du registre partagé et agissent sur la bonne instance. Si le point d’entrée CLI prévu par l’intégration initiale manque, complète-le dans l’application. Il doit permettre de lister, décrire et exécuter les opérations métier, avec une sortie JSON et des codes de sortie exploitables en script. Conserve les commandes et options CLI préexistantes. Vérifie le mode sans interface pour les opérations compatibles et le ciblage de l’instance ouverte pour celles qui en dépendent.

3. Adapte les options aux nouveautés utiles

Vérifie les fonctions ci-dessous lorsqu’elles font partie du changement de version. Active-les séparément selon les besoins du projet et documente les réglages retenus.

- Découverte des outils. Vérifie l’effet du mode à la demande sur les catalogues importants, y compris les changements de comportement par défaut du SDK. L’assistant doit pouvoir trouver et appeler les opérations autorisées. Les clients qui dépendent du catalogue complet doivent conserver ce parcours.
- Ressources, grep et gros volumes. Utilise la recherche, la pagination et les lectures par morceaux quand elles évitent de transmettre des données inutiles. Si des résultats dépassent régulièrement le budget mémoire, adapte leur production et configure le stockage temporaire privé sur disque. Préserve l’accès au résultat complet pendant la durée prévue. Les résultats destinés à survivre à un redémarrage restent stockés selon les règles de persistance de l’application.
- Cache et notifications. Si les outils ou les ressources changent pendant une session, utilise les notifications sur les transports compatibles. Vérifie le rafraîchissement du cache et garde le TTL ou le rafraîchissement explicite pour les anciens clients. Traite les invalidations, erreurs et expirations sans rejouer une action déjà exécutée.
- Logs de performance. Si le projet en a besoin, active les mesures par opération et raccorde-les au journal existant ou à un fichier JSONL privé. Enregistre uniquement les métadonnées, comme la durée, les volumes, le statut et les accès au cache.
- Conversations longues. Pour les API dont l’application gère l’historique, configure une fenêtre de contexte si nécessaire. Conserve l’historique et les sauvegardes complets. Garde les instructions système, le tour actif et les échanges d’outils complets dans la fenêtre appropriée. Garde le résumé et la limite configurables. Les agents CLI gèrent leur contexte nativement.
- MCP externes. Si l’application consomme les outils d’autres serveurs, vérifie la découverte, les permissions et la transmission des résultats et des erreurs après la mise à jour.

4. Valide la mise à jour en local

Compile et lance l’application. Exécute les tests localement, sans workflow GitHub. Teste la configuration existante après mise à jour, puis les nouvelles options effectivement activées.

- Vérifie les parcours métier concernés et l’ouverture des anciennes configurations et données utilisées par l’application.
- Depuis le chat, vérifie une réponse progressive, une annulation et un appel d’outil qui agit sur l’état réel de l’application.
- Depuis le CLI, vérifie les commandes existantes, l’aide, la découverte, la description et un appel scripté. Contrôle la sortie JSON, les codes de sortie et le ciblage de l’instance.
- Depuis un client MCP externe, vérifie la découverte, une lecture, une modification autorisée et un refus d’accès. Vérifie la cohérence des droits entre le chat, le CLI et MCP.
- Si l’application consomme un serveur MCP externe, teste un appel réussi et une erreur distante.
- Pour chaque optimisation activée, teste le comportement attendu et son principal cas d’échec. Par exemple, récupère un gros résultat octet pour octet, vérifie qu’un changement invalide le cache avant son expiration ou confirme qu’un contexte réduit laisse l’historique complet accessible.

Corrige les régressions et rejoue les tests concernés. Mets à jour les procédures de build, de configuration, d’utilisation et de packaging qui ont changé. Dans le compte rendu, indique les versions et révisions de départ et d’arrivée, les adaptations effectuées, les options activées, les tests réussis et les limites vérifiées. Donne au moins une commande CLI réellement exécutée sur l’application mise à jour.
```
