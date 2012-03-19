var config = require('../config'),
    db = require('../db'),
    fs = require('fs'),
    path = require('path'),
    pix = require('../server/pix');

function Recycler() {
	this.tag = 'archive';
	this.y = new db.Yakusoku(this.tag, db.UPKEEP_IDENT);
}

var R = Recycler.prototype;

R.recycle_post = function (post, cb) {
	if (!post.image || !post.image.src)
		return cb(null);
	var r = this.y.connect();
	var image = post.image
	var src = pix.media_path('src', image.src);
	var toDelete = [];
	if (image.thumb) {
		toDelete.push(src);
		src = pix.media_path('thumb', image.thumb);
	}
	if (image.realthumb) {
		toDelete.push(pix.media_path('thumb', image.realthumb));
	}

	pix.MD5_file(src, function (err, MD5) {
		if (err) {
			console.warn(src + " doesn't exist.");
			return cb(null);
		}
		var dest = MD5 + path.extname(src);
		var dest_path = path.join(config.MEDIA_DIRS.vint, dest);
		pix.mv_file(src, dest_path, function (err) {
			if (err)
				return cb(err);
			var m = r.multi();
			var key = post.op ? 'post:' + post.num
					: 'thread:' + post.num;
			m.hdel(key, 'src');
			m.hdel(key, 'thumb');
			m.hset(key, 'vint', dest);
			m.exec(function (err) {
				if (err) {
					// move it back
					pix.mv_file(dest_path, src,
							function (e) {
						if (e)
							console.error(e);
						return cb(err);
					});
					return;
				}

				toDelete.forEach(function (victim) {
					fs.unlink(victim, function (err) {
						if (err)
							console.error(err);
					});
				});
				if (toDelete.length) {
					var info = post.num + ': del ' +
							toDelete.length;
					console.log(info);
				}

				cb(null);
			});
		});
	});
};

R.recycle_thread = function (op, cb) {
	op = parseInt(op, 10);
	var reader = new db.Reader(this.y);
	reader.get_thread(this.tag, op, {});
	var do_post = this.recycle_post.bind(this);
	reader.on('thread', function (thread) {
		if (thread.immortal)
			return cb(null);
		// grrr, ought to stream
		var posts = [thread];
		reader.on('post', function (post) {
			posts.push(post);
		});
		reader.on('endthread', function () {
			forEachSeries(posts, do_post, cb);
		});
		reader.on('error', cb);
	});
};

R.recycle_archive = function (cb) {
	var key = 'tag:' + this.tag.length + ':' + this.tag;
	var r = this.y.connect();
	var do_thread = this.recycle_thread.bind(this);
	r.zrange(key + ':threads', 0, -1, function (err, threads) {
		if (err)
			return cb(err);
		forEachSeries(threads, do_thread, cb);
	});
};

// avoids stack overflow for long lists
function forEachSeries(array, func, callback) {
	step(0);
	function step(i) {
		if (i >= array.length)
			return callback(null);
		func(array[i], function (err) {
			if (err)
				return callback(err);
			setTimeout(step.bind(null, i + 1), 0);
		});
	}
}

if (require.main === module) {
	var recycler = new Recycler;
	recycler.recycle_archive(function (err) {
		if (err) throw err;
		recycler.y.disconnect();
		process.exit(0);
	});
}