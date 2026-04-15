
#include <asm/pgtable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pgtable.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

static int pid = 1;

static void* get_next_table(unsigned long val) {
    unsigned long pfn = (val >> _PAGE_PFN_SHIFT);
    if (!pfn_valid(pfn))
        return NULL;
    return pfn_to_virt(pfn);
}

static void print_pgtree(struct seq_file* file, int pid) {
  struct task_struct* task;

  task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
  if (!task) {
    seq_printf(file, "Process %d not found\n", pid);
    return;
  }

  if (!task->mm) {
    seq_printf(file, "MM not found for process %d\n", pid);
    put_task_struct(task);
    return;
  }

  seq_printf(file, "Page tree for pid %d (%s):\n\n", pid, task->comm);

  mmap_read_lock(task->mm);
  
  for (int i = 0; i < PTRS_PER_PGD; ++i) {
    pgd_t pgd = task->mm->pgd[i];
    if (pgd_none(pgd)) continue;

    seq_printf(file, "PGD %03d (value: %lx)\n", i, (unsigned long)pgd_val(pgd));
    if (pgd_leaf(pgd)) continue;

    int p4d_count;
    p4d_t* p4d_arr;
    if (mm_p4d_folded(task->mm)) {
      p4d_count = 1;
      p4d_arr = (p4d_t*)&pgd;
    } else {
      p4d_count = PTRS_PER_P4D;
      p4d_arr = (p4d_t*)get_next_table(pgd_val(pgd));
    }
    if (!p4d_arr) continue;

    for (int j = 0; j < p4d_count; ++j) {
      p4d_t p4d = p4d_arr[j];
      if (p4d_none(p4d)) continue;

      int pud_count;
      pud_t* pud_arr;

      seq_printf(file, "-- P4D %03d (value: %ld)", j, (unsigned long)p4d_val(p4d));
      if (mm_p4d_folded(task->mm)) {
        seq_printf(file, " (folded)");
      }
      seq_printf(file, "\n");
      if (p4d_leaf(p4d)) continue;

      if (mm_pud_folded(task->mm)) {
        pud_count = 1;
        pud_arr = (pud_t*)&p4d;
      } else {
        pud_count = PTRS_PER_PUD;
        pud_arr = (pud_t *)get_next_table(p4d_val(p4d));
      }

      if (!pud_arr) continue;

      for (int k = 0; k < pud_count; ++k) {
        pud_t pud = pud_arr[k];
        if (pud_none(pud)) continue;

        seq_printf(file, "---- PUD %03d (value: %lx)", k, (unsigned long)pud_val(pud));
        if (mm_pud_folded(task->mm)) seq_printf(file, " (folded)");
        seq_printf(file, "\n");
        if (pud_leaf(pud)) continue;

        pmd_t* pmd_arr = (pmd_t*)get_next_table(pud_val(pud));
        if (!pmd_arr) continue;

        for (int s = 0; s < PTRS_PER_PMD; ++s) {
          pmd_t pmd = pmd_arr[s];
          if (pmd_none(pmd)) continue;

          seq_printf(file, "------ PMD %03d (value: %lx)\n", s, (unsigned long)pmd_val(pmd));
          if (pmd_leaf(pmd)) continue;

          pte_t* pte_arr = (pte_t*)get_next_table(pmd_val(pmd));
          if (!pte_arr) continue;

          for (int t = 0; t < PTRS_PER_PTE; ++t) {
            pte_t pte = pte_arr[t];
            if (pte_none(pte)) continue;
            seq_printf(file, "-------- PTE %03d (PFN: %lx)", t, (unsigned long)pte_pfn(pte));
            seq_printf(file, " %c%c%c\n", (pte_val(pte) & _PAGE_READ) ? 'r' : '-', (pte_val(pte) & _PAGE_WRITE) ? 'w' : '-', (pte_val(pte) & _PAGE_EXEC) ? 'x' : '-');
          }
        }
      }
    }
  }

  mmap_read_unlock(task->mm);
  put_task_struct(task);
}

static int pgtree_show(struct seq_file* file, void* value) {
  print_pgtree(file, pid);
  return 0;
}

static int pgtree_open(struct inode* inode, struct file* file) {
  return single_open(file, pgtree_show, NULL);
}

static ssize_t pgtree_write(struct file *file, const char __user *buf, size_t count, loff_t*) {
    char kbuf[16];
    if (count > sizeof(kbuf) - 1) return -EINVAL;
    if (copy_from_user(kbuf, buf, count)) return -EINVAL;
    kbuf[count] = '\0';

    int new_pid;
    if (kstrtoint(kbuf, 10, &new_pid) == 0) {
        pid = new_pid;
    }

    return count;
}

static const struct proc_ops pgtree_ops = {
    .proc_open    = pgtree_open,
    .proc_read    = seq_read,
    .proc_write   = pgtree_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init pgtree_init(void) {
    if (!proc_create("pgtree", 0666, NULL, &pgtree_ops)) {
        return -ENOMEM;
    }
    return 0;
}

static void __exit pgtree_exit(void) {
    remove_proc_entry("pgtree", NULL);
}

module_init(pgtree_init);
module_exit(pgtree_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pgtree module");