#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>

asmlinkage long sys_hello(void)
{
    printk("Hello, World!\n");
    return 0;
}

asmlinkage long sys_set_weight(int weight)
{
    if (weight < 0) {
        return -EINVAL;
    }
    current->weight = weight;
    return 0;
}

asmlinkage long sys_get_weight(void)
{
    return current->weight;
}

static long get_child_leaf_sum(struct task_struct* root)
{
    long sum = 0;
    struct task_struct* child;
    struct list_head* children_list;
    if (list_empty(&root->children)) {
        return root->weight;
    }
    list_for_each(children_list, &root->children)
    {
        child = list_entry(children_list, struct task_struct, sibling);
        sum += get_child_leaf_sum(child);
    }
    return sum;
}

asmlinkage long sys_get_leaf_children_sum(void)
{
    if (list_empty(&current->children)) {
        return -ECHILD;
    }
    return get_child_leaf_sum(current);
}

static void heaviest_ancestor_weight(struct task_struct* root, pid_t* max_pid, long* max_weight)
{
    struct task_struct* child;
    struct list_head* children_list;
    if (list_empty(&root->children)) {
        goto skip_iteration;
    }
    list_for_each(children_list, &root->children)
    {
        child = list_entry(children_list, struct task_struct, sibling);
        heaviest_ancestor_weight(child, max_pid, max_weight);
    }
skip_iteration:
    if (root->weight > *max_weight) {
        *max_weight = root->weight;
        *max_pid = root->pid;
    }
}

asmlinkage long sys_get_heaviest_ancestor(void)
{
    pid_t max_pid;
    long max_weight = -1;
    heaviest_ancestor_weight(current, &max_pid, &max_weight);
    return max_pid;
}
