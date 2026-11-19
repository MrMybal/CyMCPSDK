# Prompt d’intégration de CyMCPSDK

Copier le bloc ci-dessous dans l’assistant qui travaille sur l’application hôte. Renseigner les chemins des dépôts locaux si l’assistant ne les connaît pas déjà.

```text
Intègre CyMCPSDK à cette application. Je veux pouvoir utiliser un assistant intégré qui agit sur les fonctions métier, piloter ces fonctions depuis un terminal ou une IA externe via MCP et, si le projet en a besoin, utiliser dans le chat les outils d’autres applications. Conserve les fonctionnalités actuelles de l’application, y compris ses commandes et options de lancement existantes.

Utilise en priorité les sources de la copie locale de CyMCPSDK indiquée pour ce projet ou située dans ../CyMCPSDK. Récupère les sources depuis https://github.com/MrMybal/CyMCPSDK uniquement si la copie locale est indisponible. Compile le SDK depuis ses sources et indique la version et la révision utilisées. Pour une application ou un plugin Unreal, consulte la copie locale de CyUEMCPSDK, par exemple ../CyUEMCPSDK, comme référence d’intégration.

Intègre les modules dont l’application a besoin. Garde les optimisations réglables séparément et vérifie les capacités disponibles dans les transports et la révision du SDK utilisés.

1. Remplace CyToolsCore s’il est présent

Repère ses usages directs et indirects dans le code, les dépendances, les configurations, les scripts de build et le packaging. Migre-les vers CyMCPSDK en préservant les comportements utilisés et la compatibilité avec les données existantes.

Si l’application utilise les jobs de CyToolsCore, reprends les mécanismes nécessaires de suivi, d’annulation, de persistance et de récupération des résultats. Utilise un stockage durable pour les résultats qui doivent survivre à un redémarrage ou rester accessibles au-delà de la durée de rétention du SDK.

Une fois la migration vérifiée, retire CyToolsCore et les composants dédiés à son intégration. Si CyToolsCore est absent, passe à l’étape suivante.

2. Expose les fonctions métier dans un registre partagé

Recense les fonctions accessibles à l’utilisateur et identifie celles qui se prêtent au pilotage par IA ou par script. Pour chaque opération exposée, définis un nom stable, une description indiquant quand l’utiliser, des schémas d’entrée et de sortie, et un handler qui appelle le vrai code métier de l’application.

Utilise le même registre pour le chat intégré, le CLI et le serveur MCP. Applique les droits de l’application à chaque appel. Respecte ses règles de confirmation, de sauvegarde et d’annulation. Exécute les actions sur le thread requis par l’application ou son moteur. Documente les fonctions exposées par chaque point d’entrée et les raisons des éventuelles exclusions.

3. Rends l’application utilisable en ligne de commande

Intègre un point d’entrée CLI à l’application. Étends son CLI existant si elle en a un, en préservant ses commandes, options et usages par les scripts. Sinon, ajoute un mode CLI à son exécutable ou un exécutable console compagnon selon les besoins du projet.

Le CLI doit permettre de lister les opérations, de consulter le schéma d’une opération précise et d’exécuter les opérations exposées avec des arguments JSON. Prévois une aide, une sortie JSON exploitable par les scripts, des diagnostics sur stderr et des codes de sortie fiables. Toute action qui requiert une confirmation doit suivre les règles de l’application, y compris en mode non interactif.

Pour les actions compatibles, permets l’exécution sans ouvrir l’interface graphique. Si une action dépend de l’état d’une instance déjà ouverte, adresse l’appel à cette instance et documente comment s’y connecter. Pour Unreal, choisis le point d’entrée selon le contexte de l’action. Un commandlet peut servir à un traitement autonome. Une connexion à l’éditeur peut servir à agir sur la scène ouverte.

Reprends cymcp_example comme référence pour la découverte et l’invocation directe. Les commandes livrées doivent agir sur la véritable application. Documente leur syntaxe, le choix de l’instance et les modes disponibles.

4. Intègre le chat et le serveur MCP

Réutilise CyMCP::ChatUI si l’interface est compatible avec CyUI et ImGui. Sinon, ajoute un panneau dans l’interface existante en utilisant le service Conversation de CyMCPSDK.

Configure un fournisseur capable d’appeler les outils par un transport qui les prend en charge. Affiche les réponses au fil de leur arrivée, présente les erreurs de connexion ou de génération et permets l’annulation. Intègre l’historique, la restauration de session, les pièces jointes, la détection des fournisseurs et les modèles locaux selon les besoins de l’application et les capacités du fournisseur.

Expose les opérations aux IA externes via le serveur MCP. Documente le lancement ou la connexion, le transport, la configuration et l’authentification applicables. Le client externe doit pouvoir découvrir les outils et agir sur l’état réel de l’application.

5. Configure les optimisations utiles au projet

- Découverte à la demande. Pour un catalogue important, utilise le mode compact afin que l’assistant puisse rechercher une commande, charger son schéma complet puis l’appeler. Garde la découverte complète accessible aux clients qui en ont besoin.
- Recherche et lecture ciblées. Utilise les ressources MCP et le grep natif pour les textes exposés par l’application. Active ripgrep si la recherche dans des fichiers est utile, avec un dossier explicitement choisi par l’hôte. Pour les données structurées, ajoute des filtres et de la pagination là où ils évitent de produire des résultats excessifs.
- Gros volumes. Produis les exports et résultats volumineux progressivement. Utilise les références privées et les lectures par morceaux. Active le stockage temporaire sur disque si les volumes le justifient et configure ses quotas, sa rétention et son dossier. Garde le contenu complet récupérable pendant la rétention prévue. Traite une référence expirée sans rejouer l’action.
- Cache et notifications. Utilise le cache MCP pour la découverte et les lectures compatibles. Si l’état évolue pendant une connexion, relie les outils et ressources au journal de changements et active les abonnements sur les transports compatibles. Garde le TTL et le rafraîchissement explicite pour les autres clients. Écarte toute mise en cache ou réexécution automatique des mutations.
- Performance. Rends le suivi des durées, volumes, statuts et accès au cache activable dans le journal de l’application ou dans un fichier JSONL privé. Enregistre uniquement les métadonnées nécessaires, en excluant les messages, résultats, identifiants sensibles et secrets.
- Conversations longues. Pour les API dont l’application gère l’historique, propose une fenêtre de contexte réglable. Conserve l’historique complet dans l’application. Garde les instructions système et le tour actif dans le contexte, avec les appels d’outils et leurs résultats groupés. Le résumé reste facultatif et fourni par l’hôte. Les agents CLI conservent leur gestion native du contexte.

6. Connecte les outils d’autres applications si le projet en a besoin

Utilise McpClient pour les serveurs MCP configurés par l’utilisateur. Découvre leurs outils et rends disponibles dans le chat ceux que l’application autorise explicitement. Fais remonter les résultats et les erreurs sous une forme exploitable. Utilise les ressources, la pagination et les notifications des serveurs externes lorsqu’ils les prennent en charge.

7. Vérifie l’intégration de bout en bout

Compile et lance l’application. Exécute les tests localement, sans workflow GitHub. Rejoue les parcours métier touchés par la migration.

- Chat intégré. Vérifie l’affichage progressif, l’annulation et un appel d’outil qui agit sur l’état réel de l’application.
- CLI. Vérifie l’aide, les commandes existantes, la découverte, la description et l’exécution scriptée. Contrôle la sortie JSON et les codes de sortie. Teste le mode sans interface ou le pilotage de l’instance ouverte selon les modes livrés.
- MCP externe. Vérifie la découverte et l’appel des opérations sur la vraie instance. Teste une lecture, une modification autorisée et un refus d’accès. Vérifie la cohérence des droits avec le CLI.
- Outils distants. Si la connexion a été intégrée, appelle un outil distant depuis le chat et vérifie le traitement d’une erreur.
- Optimisations activées. Vérifie les parcours concernés, par exemple la récupération complète d’un gros résultat, l’invalidation du cache avant expiration, l’absence de contenu sensible dans les mesures ou la conservation de l’historique lors d’une réduction du contexte.

Si CyToolsCore était présent, vérifie sa suppression du code, des dépendances, de la configuration et du paquet final. Indique la révision de CyMCPSDK utilisée, les modules et options activés, les commandes livrées, la procédure de connexion MCP, les tests exécutés et les limites restantes. Donne au moins un exemple de commande CLI réellement testé sur l’application.
```

Pour un projet déjà intégré, utiliser le [prompt de mise à jour](UpdatePrompt.fr.md).
