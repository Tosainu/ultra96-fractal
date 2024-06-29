How to update those files:

1. Export the block design as Tcl file using the command below:
    ```
    Vivado% write_bd_tcl -exclude_layout -make_local -exclude_pfm -no_project_wrapper <filename>
    ```
2. Modify the script a bit:
    ```
    $ sed -i 's/^\(proc cr_\w\+\).\+$/\1 { srcset {parentCell ""} } {/; s/\bcreate_bd_design\b/\0 -srcset $srcset/' <filename>
    ```
