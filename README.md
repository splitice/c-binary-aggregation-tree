c-binary-aggregation-tree
=========================

A binary aggregation tree for IPv4 addresses

Used to store values that are largely common between various CIDRs e.g IP settings.

Tree is implemented as a binary search tree, with values propagating to parent as the minimum of all descendants.

Example usage in example-usage.cpp