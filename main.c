#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libfreenect.h>
#include <inttypes.h>

#include <pthread.h>

#if defined(__APPLE__)
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/glut.h>
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <math.h>

char prenom[15];

pthread_t freenect_thread;
volatile int die = 0;

int g_argc;
char **g_argv;

int window;

pthread_mutex_t gl_backbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

// back: owned by libfreenect (implicit for depth)
// mid: owned by callbacks, "latest frame ready"
// front: owned by GL, "currently being drawn"
uint8_t *depth_mid, *depth_front;
uint8_t *rgb_back, *rgb_mid, *rgb_front;

GLuint gl_depth_tex;
GLuint gl_rgb_tex;

freenect_context *f_ctx;
freenect_device *f_dev;
int freenect_angle = 0;
int freenect_led;

freenect_video_format requested_format = FREENECT_VIDEO_RGB;
freenect_video_format current_format = FREENECT_VIDEO_RGB;

pthread_cond_t gl_frame_cond = PTHREAD_COND_INITIALIZER;
int got_rgb = 0;
int got_depth = 0;

void DrawGLScene()
{
	pthread_mutex_lock(&gl_backbuf_mutex);

	// When using YUV_RGB mode, RGB frames only arrive at 15Hz, so we shouldn't force them to draw in lock-step.
	// However, this is CPU/GPU intensive when we are receiving frames in lockstep.
	if (current_format == FREENECT_VIDEO_YUV_RGB) {
		while (!got_depth && !got_rgb) {
			pthread_cond_wait(&gl_frame_cond, &gl_backbuf_mutex);
		}
	} else {
		while ((!got_depth || !got_rgb) && requested_format != current_format) {
			pthread_cond_wait(&gl_frame_cond, &gl_backbuf_mutex);
		}
	}

	if (requested_format != current_format) {
		pthread_mutex_unlock(&gl_backbuf_mutex);
		return;
	}

	uint8_t *tmp;

	if (got_depth) {
		tmp = depth_front;
		depth_front = depth_mid;
		depth_mid = tmp;
		got_depth = 0;
	}
	if (got_rgb) {
		tmp = rgb_front;
		rgb_front = rgb_mid;
		rgb_mid = tmp;
		got_rgb = 0;
	}

	pthread_mutex_unlock(&gl_backbuf_mutex);

	glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, depth_front);

	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0,0,0);
	glTexCoord2f(1, 0); glVertex3f(640,0,0);
	glTexCoord2f(1, 1); glVertex3f(640,480,0);
	glTexCoord2f(0, 1); glVertex3f(0,480,0);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
	if (current_format == FREENECT_VIDEO_RGB || current_format == FREENECT_VIDEO_YUV_RGB)
		glTexImage2D(GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_front);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, 1, 640, 480, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, rgb_front+640*4);

	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(640,0,0);
	glTexCoord2f(1, 0); glVertex3f(1280,0,0);
	glTexCoord2f(1, 1); glVertex3f(1280,480,0);
	glTexCoord2f(0, 1); glVertex3f(640,480,0);
	glEnd();

	glutSwapBuffers();
}

void keyPressed(unsigned char key, int x, int y)
{
	if (key == 27) {
		die = 1;
		pthread_join(freenect_thread, NULL);
		glutDestroyWindow(window);
		free(depth_mid);
		free(depth_front);
		free(rgb_back);
		free(rgb_mid);
		free(rgb_front);
		// Not pthread_exit because OSX leaves a thread lying around and doesn't exit
		exit(0);
	}
	if (key == 'w') {
		freenect_angle++;
		if (freenect_angle > 30) {
			freenect_angle = 30;
		}
	}
	if (key == 's') {
		freenect_angle = 0;
	}
	if (key == 'f') {
		if (requested_format == FREENECT_VIDEO_IR_8BIT)
			requested_format = FREENECT_VIDEO_RGB;
		else if (requested_format == FREENECT_VIDEO_RGB)
			requested_format = FREENECT_VIDEO_YUV_RGB;
		else
			requested_format = FREENECT_VIDEO_IR_8BIT;
	}
	if (key == 'x') {
		freenect_angle--;
		if (freenect_angle < -30) {
			freenect_angle = -30;
		}
	}
	if (key == '1') {
		freenect_set_led(f_dev,LED_GREEN);
	}
	if (key == '2') {
		freenect_set_led(f_dev,LED_RED);
	}
	if (key == '3') {
		freenect_set_led(f_dev,LED_YELLOW);
	}
	if (key == '4') {
		freenect_set_led(f_dev,LED_BLINK_GREEN);
	}
	if (key == '5') {
		// 5 is the same as 4
		freenect_set_led(f_dev,LED_BLINK_GREEN);
	}
	if (key == '6') {
		freenect_set_led(f_dev,LED_BLINK_RED_YELLOW);
	}
	if (key == '0') {
		freenect_set_led(f_dev,LED_OFF);
	}
	if (key == 'a'){
		printf("Entrez le nom de la personne à ficher");
		scanf("%s", prenom);
		printf("Salut, %s\n", prenom);
		/*boardToFile(values,8); // on envoie les valeurs de profondeur dans le fichier*/
	}
	freenect_set_tilt_degs(f_dev,freenect_angle);
}

void ReSizeGLScene(int Width, int Height)
{
	glViewport(0,0,Width,Height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho (0, 1280, 480, 0, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void InitGL(int Width, int Height)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glShadeModel(GL_FLAT);

	glGenTextures(1, &gl_depth_tex);
	glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenTextures(1, &gl_rgb_tex);
	glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	ReSizeGLScene(Width, Height);
}

void *gl_threadfunc(void *arg)
{
	printf("GL thread\n");

	glutInit(&g_argc, g_argv);

	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
	glutInitWindowSize(1280, 480);
	glutInitWindowPosition(0, 0);

	window = glutCreateWindow("LibFreenect");

	glutDisplayFunc(&DrawGLScene);
	glutIdleFunc(&DrawGLScene);
	glutReshapeFunc(&ReSizeGLScene);
	glutKeyboardFunc(&keyPressed);

	InitGL(1280, 480);

	glutMainLoop();

	return NULL;
}

uint16_t t_gamma[2048];

// passe de coordonnees en 2D en indice tableau 1D
int getIndiceOfTab(int x, int y) {
        int indice;
        indice = (y*640) + x;
        return indice;
}

// Fait l'inverse de la fonction ci-dessus (1D vers 2D)
void get_X_Y(int indice, int* x, int* y){

	*x = indice % 640;
	*y = indice / 640; 
}

// retourne l'indice du point le plus proche de la caméra
void detectNoze(uint16_t* board, int *X, int *Y){
	
	int i;
	int min=800;
	int plusProche;
	
	for(i=0; i<640*480; i++){
		
		if(board[i] < min && board[i] > 600){
			
			get_X_Y(i,X,Y);
			
			if(*X > 250 && *X < 490){
				min = board[i];
				plusProche =i;
			}
		}
	}
	
	get_X_Y(plusProche,X,Y);
	
}

// Détection Milieu des yeux à partir du nez
void detectForeHead(uint16_t* board, int *X, int *Y){
	
	int i=0;
	int k = *Y;
	
	if(k >= 40){ 
		for(i=k; i > k-40; i--){
	
			if(board[getIndiceOfTab(*X,i-2)] <= board[getIndiceOfTab(*X,i)]){
			
				*Y = i-2;
				//printf("VAR****VAR****VAR****VAR****VAR****VAR****VAR****VAR  YYY == %"PRIu16"", *Y);
			}
		}
		
		//printf("WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN");
	}
}


void detectChin(uint16_t* board, int *X, int *Y){

	int k = *Y + 7;
	int i = k;

	if(k <= 440){
		
		for(i=k; i < k+40; i++){
				if(board[getIndiceOfTab(*X,i+2)] <= board[getIndiceOfTab(*X,i)]){
				
					*Y = i+2;
					//printf("VAR****VAR****VAR****VAR****VAR****VAR****VAR****VAR  YYY == %"PRIu16"", *Y);
				}	
		}
	}
}

void detectLeftCheek(uint16_t* board, int *X, int *Y){
	
	int i=0;
	int k = *X;
	
	if(k > 50 && k < 590){
		
		for(i=k; i < k+30; i++){
				if(board[getIndiceOfTab(i+3,*Y-4)] <= board[getIndiceOfTab(i,*Y-4)]){
						*X = i+3;
				}
		}
	}
}

void detectRightCheek(uint16_t* board, int *X, int *Y){
	
	int k = *X;
	int i=k;
	
	if(k > 50 && k < 590){
		
		for(i=k; i < k-30; i--){
				if(board[getIndiceOfTab(i-2,*Y-4)] >= board[getIndiceOfTab(i,*Y-4)]){
						*X = i+2;
				}
		}
	}
}


// Retourne 0 si main gauche détectée, 0 sinon
/*
int detectLeftHand(uint16_t board){

		int i; // parcours tableau
	
		if(board[getIndiceOfTab(40,70) < 700 || board[getIndiceOfTab(40,70) > 590){
		
				printf("\n *** LEFT HAND DETECTED *** \n");
				return 1;
		}
		else{ 
			return 0;
		}
}*/

// Retourne 0 si main droite détectée, 0 sinon
int detectRightHand(uint16_t *board){

	
		if((board[getIndiceOfTab(40,70)] < 700) && (board[getIndiceOfTab(40,70)] > 590)){
		
				printf("\n*** RIGHT HAND DETECTED ***\n");
				return 1;
		}
		else{ 
			return 0;
		}
}

/* Prend les valeurs de valaue[] et les mets dans le fichier + nom de la personne au début */

void saveNewFace(FILE *fp,char *nameArray, uint16_t *depthArray, int size){
	
	int i=0;
	
	fp = fopen("test", "w+");
	
	if(!fp){
		printf("error opening file, exiting program");
		exit(-1);
	}

	fprintf(fp,"%s\n",nameArray); // stockage du nom au début du fichier

	for(i=0; i<size; i++){
		fprintf(fp,"%d\n",depthArray[i]); // puis stockage profondeurs
	}

	fclose(fp); // fermeture fichier

	printf("End of storage function");
	
}


/* Récupère nom de la personne + met les valeurs dans le tableau 
void readFromFile(uint16_t board, int size, FILE *fp){
	
	
}*/

/*
void detectSupLip(uint16_t* board, int *X, int *Y){

	int k = *Y + 7;
	int i = k;

	
	
	if(k <= 440){
		
		for(i=k; i < k+40; i++){
				if(board[getIndiceOfTab(*X,i+2)] <= board[getIndiceOfTab(*X,i)]){
				
					*Y = i+2;
					//printf("VAR****VAR****VAR****VAR****VAR****VAR****VAR****VAR  YYY == %"PRIu16"", *Y);
				}	
				//delta = abs(board[getIndiceOfTab(*X,i+2)] - board[getIndiceOfTab(*X,i)]);
		}
		
		printf("WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN---WIN");
	}
	
	printf("fail ==> foreHead search");
	
}*/

// Retourne 1 si les valeurs correspondent
/*int compare(uint16_t board, uint16_t board2, int size){

	if(aaa != bbb)
		return 0;
		
	if(aaa != bbb)
		return 0;
		
	if(aaa != bbb)
		return 0;
		
	if(aaa != bbb)
		return 0;
		
	return 1; // if all values match
}*/

/*************************************************************************************************/
/*************************************************************************************************/
/*************************************************************************************************/


/* Affichage curseur
void printCursor(uint8_t* board, int x, int y){

	int compt,compt2;

	// condition pour ne pas sortir du tableau
	if((y+15 <= 480)&&(y-15 >= 0)&&(x+15 <= 640)&&(x-15 >= 0)){
		
		// trait du bas
		for (compt=0;compt<15;compt++) {

			for (compt2=0;compt2<3;compt2++) {

			board[3*getIndiceOfTab(x+compt2,y+compt)+0] = 255;
			board[3*getIndiceOfTab(x+compt2,y+compt)+1] = 255;
			board[3*getIndiceOfTab(x+compt2,y+compt)+2] = 255;
	
			}
		}
	
		// trait du haut
		for (compt=0;compt<15;compt++) {

			for (compt2=0;compt2<3;compt2++) {

			board[3*getIndiceOfTab(x+compt2,y-compt)+0] = 255;
			board[3*getIndiceOfTab(x+compt2,y-compt)+1] = 255;
			board[3*getIndiceOfTab(x+compt2,y-compt)+2] = 255;

			}
		}

		// trait de droite
		for (compt=0;compt<15;compt++) {

			for (compt2=0;compt2<3;compt2++) {

			board[3*getIndiceOfTab(x+compt,y+compt2)+0] = 255;
			board[3*getIndiceOfTab(x+compt,y+compt2)+1] = 255;
			board[3*getIndiceOfTab(x+compt,y+compt2)+2] = 255;
		
			}
		}
	
		// trait de gauche
		for (compt=0;compt<15;compt++) {

			for (compt2=0;compt2<3;compt2++) {

			board[3*getIndiceOfTab(x-compt,y+compt2)+0] = 255;
			board[3*getIndiceOfTab(x-compt,y+compt2)+1] = 255;
			board[3*getIndiceOfTab(x-compt,y+compt2)+2] = 255;

			}
		}
		
	}
	
}
*/

// Affiche un point aux coordonnees donnees
void printPoint(uint8_t* board, int x, int y){

	int compt,compt2;

	if((y+5 <= 480)&&(y-5 >= 0)&&(x+5 <= 640)&&(x-5 >= 0)){
	
		for (compt=0;compt<2;compt++) {

			for (compt2=0;compt2<2;compt2++) {

			board[3*getIndiceOfTab(x+compt2,y+compt)+0] = 255;
			board[3*getIndiceOfTab(x+compt2,y+compt)+1] = 255;
			board[3*getIndiceOfTab(x+compt2,y+compt)+2] = 255;
	
			}
		}
	
		// trait du haut
		for (compt=0;compt<2;compt++) {

			for (compt2=0;compt2<2;compt2++) {

			board[3*getIndiceOfTab(x+compt2,y-compt)+0] = 255;
			board[3*getIndiceOfTab(x+compt2,y-compt)+1] = 255;
			board[3*getIndiceOfTab(x+compt2,y-compt)+2] = 255;

			}
		}

		// trait de droite
		for (compt=0;compt<2;compt++) {

			for (compt2=0;compt2<2;compt2++) {

			board[3*getIndiceOfTab(x+compt,y+compt2)+0] = 255;
			board[3*getIndiceOfTab(x+compt,y+compt2)+1] = 255;
			board[3*getIndiceOfTab(x+compt,y+compt2)+2] = 255;
		
			}
		}
	
		// trait de gauche
		for (compt=0;compt<2;compt++) {

			for (compt2=0;compt2<2;compt2++) {

			board[3*getIndiceOfTab(x-compt,y+compt2)+0] = 255;
			board[3*getIndiceOfTab(x-compt,y+compt2)+1] = 255;
			board[3*getIndiceOfTab(x-compt,y+compt2)+2] = 255;

			}
		}
	
	}

}

void depth_cb(freenect_device *dev, void *v_depth, uint32_t timestamp)
{
	int i;
	uint16_t *depth = (uint16_t*)v_depth; // tableau contenant les profondeurs
	
	int x,y; // coordonnees du nez
	int x1,y1,x2,y2,x3,y3,x4,y4;
	int *px = &x, *py = &y;
	int *px1 = &x1, *py1 = &y1;
	int *px2 = &x2, *py2 = &y2;
	int *px3 = &x3, *py3 = &y3;
	int *px4 = &x4, *py4 = &y4;
	
	char userName[16];
	uint16_t values[8] = {0,0,0,0,0,0,0,0};
	uint16_t fileValues[8] = {0,0,0,0,0,0,0,0};
	FILE *fp = NULL;
	
	int j=0;
	
	pthread_mutex_lock(&gl_backbuf_mutex);
	
	int indiceNez; //AD
	

	for (i=0; i<640*480; i++) {
	
		int pval = t_gamma[depth[i]];
		int lb = pval & 0xff;

		switch (pval>>8) {
			case 0:	
				depth_mid[3*i+0] = 255;
				depth_mid[3*i+1] = 255-lb;
				depth_mid[3*i+2] = 255-lb;

				break;
			case 1:
				depth_mid[3*i+0] = 255;
				depth_mid[3*i+1] = lb;
				depth_mid[3*i+2] = 0;
				
			
				break;
			case 2:
				depth_mid[3*i+0] = 255-lb;
				depth_mid[3*i+1] = 255;
				depth_mid[3*i+2] = 0;
					
				break;
			case 3:
				depth_mid[3*i+0] = 0;
				depth_mid[3*i+1] = 255;
				depth_mid[3*i+2] = lb;
					
				break;
			case 4:
				depth_mid[3*i+0] = 0;
				depth_mid[3*i+1] = 255-lb;
				depth_mid[3*i+2] = 255;

				break;
			case 5:
				depth_mid[3*i+0] = 0;
				depth_mid[3*i+1] = 0;
				depth_mid[3*i+2] = 255-lb;
					
				break;
			default:
				depth_mid[3*i+0] = 0;
				depth_mid[3*i+1] = 0;
				depth_mid[3*i+2] = 0;
	
				break;
		}
	}
	

	
	detectNoze(depth,px,py); // recherche du point le plus proche de la caméra
	values[0] = depth[getIndiceOfTab(x,y)];
	printPoint(depth_mid,x,y); // affichage du curseur sur le point le plus proche (on espère le nez)
	
	*px1 = *px;
	*py1 = *py;
	*px2 = *px;
	*py2 = *py;
	*px3 = *px;
	*py3 = *py;
	*px4 = *px;
	*py4 = *py;
	
	detectChin(depth,px1,py1); /* obligé d'utiliser x1 & y1 pour pas perdre les coordonnées du nez a utiliser pour le front */
	values[1] = depth[getIndiceOfTab(x1,y1)];
	printPoint(depth_mid,x1,y1);
	
	detectForeHead(depth,px2,py2);
	values[2] = depth[getIndiceOfTab(x2,y2)];
	printPoint(depth_mid,x2,y2);
	
	detectRightCheek(depth,px3,py3);
	values[3] = depth[getIndiceOfTab(x3,y3)];
	printPoint(depth_mid,x3,y3);
	
	/*
	detectLeftCheek(depth,px4,py4);
	values[4] = depth[getIndiceOfTab(x4,y4)];
	printPoint(depth_mid,x4,y4);
	*/
	if(detectRightHand(depth) == 1){
		printf("\nENTER YOUR NAME\n");
		scanf("%s", userName);
		printf("\n");
		saveNewFace(fp,userName,values,8);
	}
	
	/*
	if(detectLeftHand(depth) == 1){
		
		readFromFile();
	
		if(compare(values1[],values2[]) == 1){
			printf("+++++++++++  KINECT DETECTED JOHN  +++++++++++");
		}
	
	}*/
	
	
	//printf("nez = %d\n autre = %d\n ",depth[getIndiceOfTab(x,y)], depth[getIndiceOfTab(x,y-25)]);
	//x= x+15;
	//y= y+25;
	
	//printPoint(depth_mid,x+15,y+15); // test 2ème curseur
	
	//int fin=getIndiceOfTab( x+ (-1)*90 , y+ 120 );
	//int debut=getIndiceOfTab( x+ 90 , y+ (-1)*120);
	//printf("coin sup gauche =%d \n coin sup droit =%d\n coin inf gauche=%d\n coin inf droit=%d\n",getIndiceOfTab(1,1), getIndiceOfTab(639,1), getIndiceOfTab(1,479), getIndiceOfTab(639,479));

	got_depth++;
	pthread_cond_signal(&gl_frame_cond);
	pthread_mutex_unlock(&gl_backbuf_mutex);
}

void rgb_cb(freenect_device *dev, void *rgb, uint32_t timestamp)
{
	pthread_mutex_lock(&gl_backbuf_mutex);

	// swap buffers
	assert (rgb_back == rgb);
	rgb_back = rgb_mid;
	freenect_set_video_buffer(dev, rgb_back);
	rgb_mid = (uint8_t*)rgb;

	got_rgb++;
	pthread_cond_signal(&gl_frame_cond);
	pthread_mutex_unlock(&gl_backbuf_mutex);
}

void *freenect_threadfunc(void *arg)
{
	int accelCount = 0;

	freenect_set_tilt_degs(f_dev,freenect_angle);
	freenect_set_led(f_dev,LED_RED);
	freenect_set_depth_callback(f_dev, depth_cb);
	freenect_set_video_callback(f_dev, rgb_cb);
	freenect_set_video_mode(f_dev, freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, current_format));
	freenect_set_depth_mode(f_dev, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT));
	freenect_set_video_buffer(f_dev, rgb_back);

	freenect_start_depth(f_dev);
	freenect_start_video(f_dev);

	printf("'w'-tilt up, 's'-level, 'x'-tilt down, '0'-'6'-select LED mode, 'f'-video format\n");

	while (!die && freenect_process_events(f_ctx) >= 0) {
		//Throttle the text output
		if (accelCount++ >= 2000)
		{
			accelCount = 0;
			freenect_raw_tilt_state* state;
			freenect_update_tilt_state(f_dev);
			state = freenect_get_tilt_state(f_dev);
			double dx,dy,dz;
			freenect_get_mks_accel(state, &dx, &dy, &dz);
			printf("\r raw acceleration: %4d %4d %4d mks acceleration: %4f %4f %4f", state->accelerometer_x, state->accelerometer_y, state->accelerometer_z, dx, dy, dz);
			fflush(stdout);
		}

		if (requested_format != current_format) {
			freenect_stop_video(f_dev);
			freenect_set_video_mode(f_dev, freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, requested_format));
			freenect_start_video(f_dev);
			current_format = requested_format;
		}
	}

	printf("\nshutting down streams...\n");

	freenect_stop_depth(f_dev);
	freenect_stop_video(f_dev);

	freenect_close_device(f_dev);
	freenect_shutdown(f_ctx);

	printf("-- done!\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int res;

	depth_mid = (uint8_t*)malloc(640*480*3);
	depth_front = (uint8_t*)malloc(640*480*3);
	rgb_back = (uint8_t*)malloc(640*480*3);
	rgb_mid = (uint8_t*)malloc(640*480*3);
	rgb_front = (uint8_t*)malloc(640*480*3);

	printf("Kinect camera test\n");

	int i;
	for (i=0; i<2048; i++) {
		float v = i/2048.0;
		v = powf(v, 3)* 6;
		t_gamma[i] = v*6*256;
	}

	g_argc = argc;
	g_argv = argv;

	if (freenect_init(&f_ctx, NULL) < 0) {
		printf("freenect_init() failed\n");
		return 1;
	}

	freenect_set_log_level(f_ctx, FREENECT_LOG_DEBUG);
	freenect_select_subdevices(f_ctx, (freenect_device_flags)(FREENECT_DEVICE_MOTOR | FREENECT_DEVICE_CAMERA));

	int nr_devices = freenect_num_devices (f_ctx);
	printf ("Number of devices found: %d\n", nr_devices);

	int user_device_number = 0;
	if (argc > 1)
		user_device_number = atoi(argv[1]);

	if (nr_devices < 1) {
		freenect_shutdown(f_ctx);
		return 1;
	}

	if (freenect_open_device(f_ctx, &f_dev, user_device_number) < 0) {
		printf("Could not open device\n");
		freenect_shutdown(f_ctx);
		return 1;
	}

	res = pthread_create(&freenect_thread, NULL, freenect_threadfunc, NULL);
	if (res) {
		printf("pthread_create failed\n");
		freenect_shutdown(f_ctx);
		return 1;
	}

	// OS X requires GLUT to run on the main thread
	gl_threadfunc(NULL);

	return 0;
}
