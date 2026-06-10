module examples.print_decimal_remote

bridge Local_to_Cluster : Local -> Cluster {
    kind = migration
}

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
    print_decimal(remote)
}
