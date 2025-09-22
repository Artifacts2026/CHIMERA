# Formal Modeling and Analysis of CHIMERA-B

## File Structure and Descriptions

### Maude environment

- Maude331 - running environment of Maude 3.3.1, obtained from [Release Maude 3.3.1 · maude-lang/Maude · GitHub](https://github.com/maude-lang/Maude/releases/tag/Maude3.3.1)

### Maude Specification Files

- chimera-n3.maude - chimera spec with 3 node

- chimera-n3-t2.maude - chimera spec with 3 node and 2 reboot

- chimera-n5.maude - chimera spec with 5 node

### Input Files

- in-n3.txt

- in-n3-t2.txt

- in-n5.txt

### Output Files

- out-n3.txt

- out-n3-t2.txt

- out-n5.txt

## Execution

Execute the order with root/sudo privileges on Linux. Results are in the output files.

```shell
nohup sh run-maude3.sh < in-n3.txt > out-n3.txt &

nohup sh run-maude3.sh < in-n3-t2.txt > out-n3-t2.txt &

nohup sh run-maude3.sh < in-n5.txt > out-n5.txt &
```
