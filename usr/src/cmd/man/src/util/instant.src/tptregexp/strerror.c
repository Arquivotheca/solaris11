
/* standin for strerror(3) which is missing on some systems
 * (eg, SUN)
 */

char *
strerror(int num)
{
	perror(num);
	return "";
}    
