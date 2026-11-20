---
id: permissions
title: Configurer les droits
description: Choisir séparément la politique des opérations hôtes et les autorisations de l'agent natif.
section: SDK
parent: integration
order: 2
sourceRevision: 2
---

## Choisir la politique hôte {#policy}

Une `Policy` par défaut refuse les opérations. Utiliser un callback pour décider à partir du
contexte, du nom de l'opération et des arguments, ou choisir explicitement `Policy::allow_all()`
lorsque l'hôte veut un accès libre à son domaine enregistré. Le SDK n'impose pas de boîtes de confirmation.

```cpp
cymcp::Policy reads_only([](const auto&, const std::string& name, const auto&) {
    return name == "get_counter" ? cymcp::Decision::allow : cymcp::Decision::deny;
});
```

Autoriser une opération ne crée pas de capacités supplémentaires. Limiter les handlers et leurs
schémas au besoin de l'application. La validation du domaine s'applique même avec une politique
permissive. L'annulation et les budgets de ressources sont distincts des décisions de droits.

## Autorisations de l'agent et opérations {#agents}

Les droits natifs de l'agent sont distincts du registre d'opérations. Dans Chat Lab,
**Autoriser toutes les actions** demande le mode permissif du fournisseur. Le **Profil d’accès MCP**
règle les droits du registre de démonstration. Le fournisseur et le système d’exploitation
appliquent leurs propres droits.

Les services client ACP de fichiers et terminal ne sont pas annoncés par défaut. L'hôte doit
déclarer `settings.client_capabilities`, fournir `request_handler` et choisir sa politique
d'autorisation. Il implémente le comportement et le nettoyage des fichiers/terminaux ; une simple
capacité déclarée ne crée aucun service de terminal.

## Authentifier un serveur local {#network}

En mode protégé, le serveur HTTP local accepte dans `Authorization: Bearer ...` un jeton fort
d’au moins 32 caractères ou un identifiant validé par le callback d’authentification de l’hôte.
Pour ouvrir explicitement l’accès local sans identifiant, utiliser `LocalHttpServer::UnauthenticatedLoopback`.
Le serveur écoute sur `127.0.0.1` et refuse les requêtes portant `Origin` dans les deux modes.

`local_access_settings(LocalAccessProfile)` fournit des profils d’exigence du jeton et de politique
métier. L’hôte applique ces réglages séparément au transport et au registre. Les
[profils de Chat Lab](doc:chat-lab#external) présentent les quatre combinaisons.
