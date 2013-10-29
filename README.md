<h1>mGet - the multi curl downloader</h1>
<h2>Requires</h2>
<hr />
<ul>
<li>ncurses-devel</li>
<li>curl-devel</li>
</ul>
<h2>Tested on</h2>
<p>Centos 6.4 x64.</p>
<h2>Build instructions</h2>
<p>./build.sh</p>
<h2>Run instructions</h2>
<p>./get http://www.domain.com/largefile.zip</p>
<p>This will fetch the file in 7 segments, join the file once it is done</p>