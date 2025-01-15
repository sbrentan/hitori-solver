# hitori-solver
Hitori solver parallel application written in C for the High Performance Computing course at University of Trento

# Usage

Follow these instructions to correctly set up the environment.

## VPN Setup
1. Download the VPN client from the following URL: [GlobalProtect](https://vpn-mfa.icts.unitn.it).
2. Install the VPN client and connect to the VPN service using the address `vpn-mfa.icts.unitn.it`. Then authenticate with your university credentials when prompted.

## SSH Visual Studio Code Setup

1. Install the **Remote - SSH** extension in Visual Studio Code.
2. Open the extension and select `Connect to Host` followed by `Configure SSH Hosts`.
3. Choose the `.ssh/config` file from the proposed SSH configuration files and add the following configuration at the end of it:

```bash
Host hpc2.unitn.it
    HostName hpc2.unitn.it
    User name.surname
```

Replace `name.surname` with your university credentials used for authentication.

## Cluster Connection
1. Use the **Remote - SSH** extension to connect to the configured SSH host.
2. When prompted, enter your university credentials.
3. After a successful login, open your dedicated folder (i.e. `name.surname`). If asked, type again the password.

## Environment adjustments
1. Since the `mpich` module is not available globally, add the following line to the end of your `.bashrc` file:

```bash
export PATH=/apps/mpich-3.2/bin:$PATH
```

2. Close and reopen the terminal, or run the following command in the root directory to apply the changes:

```bash
source .bashrc
```

## Clone the Repository
Finally, to get started with the project, clone the repository into your dedicated folder using the following command:

```bash
git clone https://github.com/sbrentan/hitori-solver.git
```

## Inside the cluster
To prevent saturation, it is **strictly forbidden** to run programs directly on the login node. Instead, to run programs, 
you can use the following three primary commands:

```bash
qsub job.sh     # Submit the job to the cluster, adhering to the specified PBS directives
qstat job_id    # Retrieve the statistics for a specific job
qdel job_id     # Cancel a job if it has not yet completed
```

Note that `job.sh` includes both the PBS directives, which instruct the cluster on how to manage the job, and the necessary 
details for execution, such as the modules to load (e.g. `mpich`) and the commands to run (e.g. `mpirun.actual ./main.out -n 8`).

# References
[Menneske](https://www.menneske.no/hitori/methods/eng/index.html)

[Python Example](https://github.com/RonMidthun/hitori.py/tree/master)

[keepitsimplepuzzles](https://www.keepitsimplepuzzles.com/how-to-solve-hitori-puzzles/#basic-techniques)

[conceptispuzzles](https://www.conceptispuzzles.com/index.aspx?uri=puzzle/hitori/techniques)
