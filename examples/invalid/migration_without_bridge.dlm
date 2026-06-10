module examples.migration_without_bridge

theory Local {
    let n = 7
}

theory Cluster {
    let arm = node_arm()
    let remote = migrate(arm, Local.n)
}
