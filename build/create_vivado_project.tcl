#=============================================================================
# create_vivado_project.tcl
#   Rebuild the Vivado project from <project>/src/pl into <project>/board/prj
#
# Usage (normally via create_vivado_project.bat):
#   vivado -mode gui   -source build/create_vivado_project.tcl [-tclargs ...]
#   vivado -mode batch -source build/create_vivado_project.tcl -tclargs ...
#
# Options:
#   -recreate      delete an existing board/prj and rebuild from scratch
#   -batch         headless: close the project and exit when done
#   -synth         run synthesis after creating the project
#   -impl          synthesis + implementation (up to route_design)
#   -bit           synthesis + implementation + write bitstream
#   -xsa <file>    like -bit, then export the hardware platform (.xsa)
#   -jobs <N>      parallel jobs (default 8)
#   -root <path>   project root (default: derived from this script's location)
#
# Expected source layout:
#   src/pl/rtl/*.v             RTL (also holds modules referenced by the BD)
#   src/pl/bd/system/          block design (system.bd + ip/ + ui/)
#   src/pl/constr/*.xdc        pin/timing constraints
#   src/pl/ip/*.xci            standalone IP (fir_compiler_0)
#   src/pl/imports/**/*.coe    coefficient files used by IP
#   src/pl/ip_repo/*           custom IP repository
#   src/pl/utils/*             extra reference files (optional)
#
# NOTE: keep this file ASCII only. Vivado reads sourced files with the system
#       code page (GBK on Chinese Windows), so non-ASCII bytes can corrupt the
#       Tcl parser (multi-byte sequence swallowing the next '[' or '"').
#=============================================================================

#---------------------------------------------------------------------------
# User configuration
#---------------------------------------------------------------------------
set PROJ_NAME     "prj"
set PART          "xc7z020clg400-1"
set BD_NAME       "system"
set TOP_MODULE    "system_wrapper"
set DEFAULT_JOBS  8
#---------------------------------------------------------------------------

proc say {msg} {
    puts "\[vivado-project\] $msg"
}

proc fail {msg} {
    error $msg
}

proc require_dir {path what} {
    if {![file isdirectory $path]} {
        fail "missing $what: $path"
    }
}

proc require_file {path what} {
    if {![file isfile $path]} {
        fail "missing $what: $path"
    }
}

# Tcl "glob -recurse" is not available in Vivado's Tcl, so walk manually.
proc find_files_recursive {dir patterns} {
    set result {}
    if {![file isdirectory $dir]} {
        return $result
    }
    foreach pattern $patterns {
        foreach f [lsort [glob -nocomplain -directory $dir $pattern]] {
            lappend result $f
        }
    }
    foreach sub [lsort [glob -nocomplain -type d -directory $dir *]] {
        foreach f [find_files_recursive $sub $patterns] {
            lappend result $f
        }
    }
    return $result
}

#---------------------------------------------------------------------------
# Option parsing
#---------------------------------------------------------------------------
set opt_root       ""
set opt_recreate   0
set opt_batch      0
set opt_synth      0
set opt_impl       0
set opt_bit        0
set opt_xsa        ""
set opt_jobs       $DEFAULT_JOBS

for {set i 0} {$i < [llength $argv]} {incr i} {
    set arg [lindex $argv $i]
    switch -- $arg {
        -root     { incr i; set opt_root [lindex $argv $i] }
        -recreate { set opt_recreate 1 }
        -batch    { set opt_batch 1 }
        -synth    { set opt_synth 1 }
        -impl     { set opt_impl 1; set opt_synth 1 }
        -bit      { set opt_bit 1; set opt_impl 1; set opt_synth 1 }
        -xsa      { incr i; set opt_xsa [lindex $argv $i]
                    set opt_bit 1; set opt_impl 1; set opt_synth 1 }
        -jobs     { incr i; set opt_jobs [lindex $argv $i] }
        default   { say "ignore unknown argument: $arg" }
    }
}

#---------------------------------------------------------------------------
# Paths
#---------------------------------------------------------------------------
if {$opt_root ne ""} {
    set root [file normalize $opt_root]
} else {
    set script_dir [file normalize [file dirname [info script]]]
    set root [file normalize [file join $script_dir ..]]
}

set pl_dir      [file join $root src pl]
set board_dir   [file join $root board]
set prj_dir     [file join $board_dir prj]
set xpr_file    [file join $prj_dir "$PROJ_NAME.xpr"]
set srcs_dir    [file join $prj_dir "$PROJ_NAME.srcs"]
set logs_dir    [file join $board_dir logs]

set bd_src_dir  [file join $pl_dir bd $BD_NAME]
set bd_src_file [file join $bd_src_dir "$BD_NAME.bd"]
set bd_dst_dir  [file join $srcs_dir sources_1 bd $BD_NAME]
set bd_dst_file [file join $bd_dst_dir "$BD_NAME.bd"]

set rtl_dir     [file join $pl_dir rtl]
set constr_dir  [file join $pl_dir constr]
set ip_dir      [file join $pl_dir ip]
set imports_dir [file join $pl_dir imports]
set repo_dir    [file join $pl_dir ip_repo]
set utils_dir   [file join $pl_dir utils]

say "root      : $root"
say "sources   : $pl_dir"
say "project   : $prj_dir"
say "vivado    : [version -short]"

#---------------------------------------------------------------------------
# Input checks
#---------------------------------------------------------------------------
require_dir  $pl_dir      "src/pl"
require_dir  $rtl_dir     "src/pl/rtl"
require_file $bd_src_file "block design"
require_dir  $constr_dir  "src/pl/constr"

set rtl_files {}
foreach pattern {*.v *.sv *.vhd *.vhdl} {
    foreach f [lsort [glob -nocomplain -directory $rtl_dir $pattern]] {
        lappend rtl_files $f
    }
}
if {[llength $rtl_files] == 0} {
    fail "no .v/.sv/.vhd source files under src/pl/rtl"
}

set xdc_files [lsort [glob -nocomplain -directory $constr_dir *.xdc]]
if {[llength $xdc_files] == 0} {
    say "warning: no .xdc under src/pl/constr"
}

set xci_files [lsort [glob -nocomplain -directory $ip_dir */*.xci]]
set coe_files [find_files_recursive $imports_dir {*.coe}]
set dcp_files [find_files_recursive $utils_dir {*.dcp}]

set repo_paths {}
if {[file isdirectory $repo_dir]} {
    foreach d [lsort [glob -nocomplain -type d [file join $repo_dir *]]] {
        lappend repo_paths $d
    }
}

say "RTL: [llength $rtl_files]  XDC: [llength $xdc_files]  IP: [llength $xci_files]  COE: [llength $coe_files]  IP repos: [llength $repo_paths]"

#---------------------------------------------------------------------------
# Create or open the project
#
# Vivado writes several files relative to the *current directory*
# (.Xil, NA, prj.gen, nlviewcommon.logfile, vivado_pid*.str ...).  Move into
# the project directory first so that all of them stay inside board/prj and
# never pollute the project root.
#---------------------------------------------------------------------------
if {$opt_recreate && [file exists $xpr_file]} {
    catch {cd $board_dir}
    say "remove old project dir: $prj_dir"
    if {[catch {file delete -force $prj_dir} err]} {
        fail "cannot delete old project dir (is Vivado using it?): $err"
    }
}
if {![file exists $prj_dir]} {
    file mkdir $prj_dir
}
if {[catch {cd $prj_dir} err]} {
    say "warning: cannot change to $prj_dir : $err"
}

if {[file exists $xpr_file]} {
    say "open existing project: $xpr_file"
    open_project $xpr_file
} else {
    say "create project: $PROJ_NAME ($PART)"
    create_project $PROJ_NAME $prj_dir -part $PART -force -quiet
}

catch {file mkdir $logs_dir}

#---------------------------------------------------------------------------
# IP repositories
#---------------------------------------------------------------------------
if {[llength $repo_paths] > 0} {
    say "set [llength $repo_paths] IP repository paths"
    set_property ip_repo_paths $repo_paths [current_project]
    if {[catch {update_ip_catalog -rebuild} err]} {
        say "warning: update_ip_catalog: $err"
    }
}

#---------------------------------------------------------------------------
# Block design (copy together with its ip/ folder so IP settings are kept)
#---------------------------------------------------------------------------
if {![file exists $bd_dst_file]} {
    say "copy block design into project: $bd_dst_dir"
    file mkdir [file dirname $bd_dst_dir]
    if {[catch {file copy -force $bd_src_dir $bd_dst_dir} err]} {
        fail "cannot copy block design: $err"
    }
}
if {[llength [get_files -quiet $bd_dst_file]] == 0} {
    say "add block design to project"
    add_files -fileset sources_1 -norecurse $bd_dst_file
}

#---------------------------------------------------------------------------
# Sources, constraints, coefficients, standalone IP
#---------------------------------------------------------------------------
say "add RTL sources"
add_files -fileset sources_1 -norecurse $rtl_files

if {[llength $xdc_files] > 0} {
    say "add constraints"
    add_files -fileset constrs_1 -norecurse $xdc_files
}

if {[llength $coe_files] > 0} {
    # Mirror src/pl/imports into the project, keeping the same relative layout.
    # The standalone IP stores its coefficient file as
    # "../../imports/EErace_prj_v6_2_26/E7_9_fir.coe", which resolves correctly
    # only when that layout exists inside the project.
    say "copy coefficient files into project"
    set imports_dst [file join $srcs_dir sources_1 imports]
    if {![file exists $imports_dst]} {
        if {[catch {file copy -force $imports_dir $imports_dst} err]} {
            say "warning: cannot copy imports: $err"
        }
    }
    set copied {}
    set src_prefix [string length [file normalize $imports_dir]]
    foreach coe $coe_files {
        set norm [file normalize $coe]
        set rel  [string range $norm [expr {$src_prefix + 1}] end]
        set dst  [file join $imports_dst $rel]
        if {[file exists $dst]} {
            lappend copied $dst
        }
    }
    if {[llength $copied] > 0} {
        set coe_files $copied
    }
    say "add coefficient files"
    if {[catch {add_files -fileset sources_1 -norecurse $coe_files} err]} {
        say "warning: cannot add .coe files: $err"
    }
}

if {[llength $xci_files] > 0} {
    # Copy the standalone IP into the project as well. If it is only referenced
    # in place, Vivado resolves the IP's output directory (prj.gen) relative to
    # the original location and writes files outside the project.
    say "copy standalone IP into project"
    set ip_dst_root [file join $srcs_dir sources_1 ip]
    file mkdir $ip_dst_root
    set ip_dst_files {}
    foreach xci $xci_files {
        set ip_name  [file rootname [file tail $xci]]
        set dst_dir  [file join $ip_dst_root $ip_name]
        set dst_xci  [file join $dst_dir [file tail $xci]]
        if {![file exists $dst_xci]} {
            if {[catch {file copy -force [file dirname $xci] $dst_dir} err]} {
                say "warning: cannot copy IP $ip_name : $err"
            }
        }
        if {[file exists $dst_xci]} {
            lappend ip_dst_files $dst_xci
        } else {
            lappend ip_dst_files $xci
        }
    }
    say "add standalone IP"
    add_files -fileset sources_1 -norecurse $ip_dst_files
}

if {[llength $dcp_files] > 0} {
    say "add reference files (utils fileset)"
    if {[llength [get_filesets -quiet utils_1]] == 0} {
        catch {create_fileset -utility utils_1}
    }
    if {[llength [get_filesets -quiet utils_1]] > 0} {
        catch {add_files -fileset utils_1 -norecurse $dcp_files}
    }
}

# fir_compiler_0 needs its coefficient file: give an absolute path so that the
# relative path stored in the .xci cannot break after moving the project.
foreach xci $xci_files {
    set ip_name [file rootname [file tail $xci]]
    if {[llength [get_ips -quiet $ip_name]] == 0} {
        continue
    }
    foreach coe $coe_files {
        if {[string match -nocase *fir* [file tail $coe]]} {
            say "set coefficient file of $ip_name : $coe"
            catch {set_property -dict [list CONFIG.Coefficient_File $coe] [get_ips $ip_name]}
            break
        }
    }
}

#---------------------------------------------------------------------------
# Generate the block design, build the wrapper, set the top
#---------------------------------------------------------------------------
set bd_file [get_files -quiet $bd_dst_file]
if {[llength $bd_file] == 0} {
    fail "block design not found in project: $bd_dst_file"
}

say "generate block design targets (this takes a few minutes)"
generate_target all $bd_file

say "create and import BD wrapper"
if {[catch {make_wrapper -files $bd_file -top -import -force} err]} {
    say "warning: make_wrapper: $err"
}

update_compile_order -fileset sources_1

set wrappers [get_files -quiet *wrapper*.v]
if {[llength $wrappers] > 0} {
    say "set top module: $TOP_MODULE"
    set_property top $TOP_MODULE [current_fileset]
} else {
    say "warning: wrapper not found, use the block design itself as top"
    set_property top $BD_NAME [current_fileset]
}
update_compile_order -fileset sources_1

say "top module     : [get_property top [current_fileset]]"
say "source files   : [llength [get_files -of_objects [get_filesets sources_1]]]"

#---------------------------------------------------------------------------
# Optional: synthesis / implementation / bitstream / XSA
#---------------------------------------------------------------------------
if {$opt_synth} {
    say "start synthesis (jobs=$opt_jobs)"
    reset_run synth_1
    launch_runs synth_1 -jobs $opt_jobs
    wait_on_run synth_1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
        fail "synthesis failed, see $prj_dir/$PROJ_NAME.runs/synth_1/runme.log"
    }
    say "synthesis done"
}

if {$opt_impl} {
    set to_step [expr {$opt_bit ? "write_bitstream" : "route_design"}]
    say "start implementation up to $to_step (jobs=$opt_jobs)"
    reset_run impl_1
    launch_runs impl_1 -to_step $to_step -jobs $opt_jobs
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
        fail "implementation failed, see $prj_dir/$PROJ_NAME.runs/impl_1/runme.log"
    }
    say "implementation done"
}

if {$opt_xsa ne ""} {
    set xsa_path [file normalize $opt_xsa]
    catch {file mkdir [file dirname $xsa_path]}
    say "export hardware platform: $xsa_path"
    write_hw_platform -fixed -include_bit -force -file $xsa_path
    say "XSA exported (the Vitis script will pick it up on its next run)"
}

#---------------------------------------------------------------------------
# Finish
#---------------------------------------------------------------------------
if {$opt_batch} {
    close_project
    say "done (batch mode). project: $xpr_file"
} else {
    say "done. project is open in Vivado: $xpr_file"
}
