<html>
	<head>
	<title>Add a Network to your wpa_supplicant.conf file</title>
	</head>
	<body>

	<h1>Enter credentials:</h1>
	<form action="addnet.php" method='post'>
	<br />
	SSID<br/>
	<input name='ssid' type='text' /><br />
	<br />
	PASSWORD<br/>
	<input name="passw" type="text" /><br />
	<br /> 
	<input type='submit' value='Add network'>
	<br />
	</form>
	<form action="<?=$_SERVER['PHP_SELF'];?>" method="post">
    	<input type="submit" name="clear_config" value="clear config">
	</form>	

	<?php
	
	$ssid = $_POST['ssid'];
	$passw = $_POST ['passw'];

	if ( isset($_POST['clear_config'])) {
		//your code here
	        shell_exec("cp /etc/wpa_supplicant/wpa_supplicant.conf_empty /etc/wpa_supplicant/wpa_supplicant.conf");
		echo "config cleared!!";
	}


	if ( isset($ssid) && isset($passw) && (strlen($passw)>7) )
		{
		exec("wpa_passphrase {$ssid} {$passw} >> /etc/wpa_supplicant/wpa_supplicant.conf"); 
		}

	// open the file in read mode 
	$f = fopen("/etc/wpa_supplicant/wpa_supplicant.conf", "rb");

	// Read file  line by line
	echo "<br />";
	echo "Contents of wpa_supplicant.conf:<br />";
	while(! feof($f))
		{
		echo fgets($f). "<br />";
		}
	// close the file
	fclose($f);
	echo "<br />";

//	echo $ssid;
	?>
 								
	</body>
</html>
