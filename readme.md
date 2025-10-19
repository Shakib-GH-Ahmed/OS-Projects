# Operating Systems Projects

This repository contains two Operating Systems course projects implemented in C for **CSE321**.

---

## 🧩 Project 1 — UNIX Shell

### Overview
Implementation of a custom UNIX shell in C that supports:
- Command execution using `fork()` and `exec()`
- Input/output redirection (`<`, `>`, `>>`)
- Command piping (`|`) with multiple stages
- Multiple commands in sequence using `;` and `&&`
- Command history tracking
- Signal handling (e.g., `CTRL+C` terminates the current process, not the shell)


---

## 🧰 Project 2 — VSFSck: File System Consistency Checker

### Overview
Implementation of a consistency checker (`vsfsck`) for a custom file system (VSFS).  
The tool verifies and repairs:
- Superblock integrity  
- Inode and data bitmap consistency  
- Duplicate block references  
- Bad block references  

### Features
- Superblock validator  
- Data bitmap consistency checker  
- Inode bitmap consistency checker  
- Duplicate and bad block detector  

---

## 🏫 Course Information
**Course:** CSE321 — Operating Systems  
**Institution:** Brac University
**Language:** C  
**Tools:** GCC / Linux Environment  

---
