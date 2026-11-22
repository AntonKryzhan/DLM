module examples.migration_bridge

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
    print_symbolic(remote)
}
