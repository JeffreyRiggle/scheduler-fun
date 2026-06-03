const { Scheduler } = require('./scheduler');
const express = require('express');
const fs = require('node:fs');

const app = express();
app.use(express.urlencoded({ extended: true }));
const scheduler = new Scheduler();
const ANON_FUNC = /=>/;
const EVIL = /\s*eval\(/;
const SPAMMER = /\s*alert\(/;

app.get('/', (req, res) => {
  res.send('<html><body><form action="file" method="post"><label for="fscript">Create a script</label><input type="text" name="fscript" required /><input type="submit" value="send"/></form></body></html>');
});

app.post('/file', (req, res) => {
  let script = req.body.fscript;
  let fileName = `./submission-${Date.now()}.js`;
  scheduler.defer(() => console.log('Got script ', script));
  scheduler.defer(() => {
    fs.writeFile(fileName, script, 'utf8', err => {
      if (err) {
        console.error('Failed to write file', err);
      }
    });
  });
  scheduler.defer(() => {
    let stats = { file: fileName, totalAnonymousFuncs: 0, evil: 0, spammer: 0 };
    const lines = script.split('\n');
    for (line of lines) {
      if (ANON_FUNC.test(line)) {
        stats.totalAnonymousFuncs++;
      }

      if (EVIL.test(line)) {
        stats.evil++;
      }

      if (SPAMMER.test(line)) {
        stats.spammer++;
      }
    }

    fs.writeFile(fileName + '.stats', JSON.stringify(stats), 'utf8', err => {
      if (err) {
        console.error('Failed to write stats file', err);
      }
    });

    if (stats.evil > 0) {
      scheduler.defer(() => {
        fs.unlink(fileName, err => {
          if (err) {
            console.error('Failed to delete file', err);
          }
        });
      });
    }
  });
  res.send(`<html><script>${script}</script></html>`);
});

app.listen(3000, () => console.log('app started'));
