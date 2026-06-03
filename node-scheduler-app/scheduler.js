class Scheduler {
  constructor() {
    this.tasks = [];
  }

  defer(task) {
    this.tasks.push(task);

    if (this.activeTimeout) return;

    this.activeTimeout = setTimeout(() => this.runNextTask(), 0);
  }

  runNextTask() {
    let task = this.tasks.splice(0, 1)[0];
    try {
      task();
    } catch (e) {
      // swallow exception
    }

    if (this.tasks.length > 0) {
      this.activeTimeout = setTimeout(() => this.runNextTask(), 0);
    } else {
      this.activeTimeout = null;
    }
  }
}

module.exports = { Scheduler };
