module examples.migration_to_non_node

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let not_node = 1
    let remote = migrate(not_node, Local.n)
}
